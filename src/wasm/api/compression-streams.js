// deno-lint-ignore-file no-node-globals no-process-global
/* eslint-disable no-unused-vars */
/* global Buffer, process, TransformStream */

// Simple, unoptimized TransformStream-like API built on WASM bindings.

function _norm(chunk) {
	if (chunk instanceof Uint8Array) return chunk;
	if (ArrayBuffer.isView(chunk)) return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
	if (chunk instanceof ArrayBuffer) return new Uint8Array(chunk);
	if (typeof Buffer !== "undefined" && Buffer.isBuffer(chunk)) return new Uint8Array(chunk);
	throw new TypeError("unsupported chunk");
}

// module-level default wasm exports (set via setWasmExports)
let _defaultWasm = null;
export function setWasmExports(obj) { _defaultWasm = obj; }

function _make(isCompress, type, options = {}) {
	const level = (typeof options.level === "number") ? options.level : -1;
	const OUT = (typeof options.outBuffer === "number") ? options.outBuffer : 64 * 1024;
	const IN_BUF = (typeof options.inBufferSize === "number") ? options.inBufferSize : 64 * 1024;

	return new TransformStream({
		start() {
			// resolution order: explicit option -> setWasmExports() -> globalThis
			this.wasm = options.wasm || _defaultWasm || globalThis.WASM_EXPORTS;
			if (!this.wasm) throw new Error("wasm required");

			this._malloc = this.wasm.malloc;
			this._free = (typeof this.wasm.free === "function") ? this.wasm.free : (typeof this.wasm._free === "function" ? this.wasm._free : null);

			// allocate fixed buffers
			this.outPtr = this._malloc(OUT);
			if (!this.outPtr) throw new Error("malloc out");
			this.inPtr = this._malloc(IN_BUF);
			if (!this.inPtr) { try { if (this._free) this._free(this.outPtr); } catch (_) { } throw new Error("malloc in"); }
			this.inPtr_sz = IN_BUF;
			this.OUT = OUT;

			this._heap = new Uint8Array(this.wasm.memory.buffer);
			this._scratch = new Uint8Array(OUT);

			// create context and select functions based on mode
			if (isCompress) {
				this.z = this.wasm.wasm_deflate_new();
				if (!this.z) throw new Error("alloc");
				if (type === "gzip" && typeof this.wasm.wasm_deflate_init_gzip === "function") {
					if (level >= 0 && typeof this.wasm.wasm_deflate_init_gzip_level === "function") {
						const r = this.wasm.wasm_deflate_init_gzip_level(this.z, level); if (r !== 0) throw new Error("deflate init(gzip) failed:" + r);
					} else {
						const r = this.wasm.wasm_deflate_init_gzip(this.z); if (r !== 0) throw new Error("deflate init(gzip) failed:" + r);
					}
				} else if (type === "deflate-raw") {
					if (level >= 0 && this.wasm.wasm_deflate_init_raw_level) this.wasm.wasm_deflate_init_raw_level(this.z, level);
					else this.wasm.wasm_deflate_init_raw(this.z);
				} else {
					if (level >= 0 && this.wasm.wasm_deflate_init_level) this.wasm.wasm_deflate_init_level(this.z, level);
					else this.wasm.wasm_deflate_init(this.z);
				}
				this._process = this.wasm.wasm_deflate_process;
				this._last_consumed = this.wasm.wasm_deflate_last_consumed;
				this._end = this.wasm.wasm_deflate_end;
			} else {
				if (type === "deflate64-raw") {
					this.z = this.wasm.wasm_inflate9_new();
					if (!this.z) throw new Error("alloc");
					this.wasm.wasm_inflate9_init_raw(this.z);
					this._process = this.wasm.wasm_inflate9_process;
					this._last_consumed = this.wasm.wasm_inflate9_last_consumed;
					this._end = this.wasm.wasm_inflate9_end;
				} else {
					this.z = this.wasm.wasm_inflate_new();
					if (!this.z) throw new Error("alloc");
					if (type === "deflate-raw") {
						const r = this.wasm.wasm_inflate_init_raw(this.z); if (r !== 0) throw new Error("inflate init failed:" + r);
					} else if (type === "gzip" && typeof this.wasm.wasm_inflate_init_gzip === "function") {
						const r = this.wasm.wasm_inflate_init_gzip(this.z); if (r !== 0) throw new Error("inflate init(gzip) failed:" + r);
					} else {
						const r = this.wasm.wasm_inflate_init(this.z); if (r !== 0) throw new Error("inflate init failed:" + r);
					}
					this._process = this.wasm.wasm_inflate_process;
					this._last_consumed = this.wasm.wasm_inflate_last_consumed;
					this._end = this.wasm.wasm_inflate_end;
				}
			}
		},

		transform(chunk, controller) {
			try {
				const buf = _norm(chunk);
				let off = 0;

				// cache hot locals to avoid repeated property lookups
				const wasm = this.wasm;
				let heap = this._heap;
				const process = this._process;
				const last_consumed = this._last_consumed;
				const outPtr = this.outPtr;
				const OUT = this.OUT;
				const scratch = this._scratch;

				// refresh heap view (memory may grow)
				if (!heap || heap.buffer !== wasm.memory.buffer) heap = this._heap = new Uint8Array(wasm.memory.buffer);

				while (off < buf.length) {
					const toRead = Math.min(buf.length - off, 32 * 1024);

					// ensure input buffer is large enough (grow if necessary)
					if (!this.inPtr || this.inPtr_sz < toRead) {
						if (this.inPtr && this._free) try { this._free(this.inPtr); } catch (_) { }
						this.inPtr = this._malloc(toRead);
						this.inPtr_sz = toRead;
						if (!this.inPtr) throw new Error("malloc");
					}

					// copy into wasm heap (single copy)
					if (!heap || heap.buffer !== wasm.memory.buffer) heap = this._heap = new Uint8Array(wasm.memory.buffer);
					heap.set(buf.subarray(off, off + toRead), this.inPtr);

					// call into wasm
					const ret = process(this.z, this.inPtr, toRead, outPtr, OUT, 0);

					if (!isCompress && ret < 0) throw new Error("inflate error:" + ret);

					const prod = ret & 0x00ffffff;
					if (prod) {
						// copy out of heap into the single scratch buffer (no intermediate allocations)
						// then create a single slice (one allocation) to hand to the consumer
						if (!heap || heap.buffer !== wasm.memory.buffer) heap = this._heap = new Uint8Array(wasm.memory.buffer);
						scratch.set(heap.subarray(outPtr, outPtr + prod), 0);
						controller.enqueue(scratch.slice(0, prod)); // unavoidable single allocation per queued chunk
					}

					const consumed = (last_consumed) ? last_consumed(this.z) : 0;
					if (consumed === 0) break;
					off += consumed;
				}
			} catch (err) {
				// cleanup on error
				try { if (this._end && this.z) this._end(this.z); } catch (_) { }
				try { if (this.inPtr && this._free) this._free(this.inPtr); } catch (_) { }
				try { if (this.outPtr && this._free) this._free(this.outPtr); } catch (_) { }
				controller.error(err);
			}
		},

		flush(controller) {
			try {
				const wasm = this.wasm;
				let heap = this._heap;
				const process = this._process;
				const outPtr = this.outPtr;
				const OUT = this.OUT;
				const scratch = this._scratch;

				if (!heap || heap.buffer !== wasm.memory.buffer) heap = this._heap = new Uint8Array(wasm.memory.buffer);

				while (true) {
					const ret = process(this.z, 0, 0, outPtr, OUT, 4);
					if (!isCompress && ret < 0) throw new Error("inflate error:" + ret);

					const prod = ret & 0x00ffffff;
					const code = (ret >> 24) & 0xff;

					if (prod) {
						if (!heap || heap.buffer !== wasm.memory.buffer) heap = this._heap = new Uint8Array(wasm.memory.buffer);
						scratch.set(heap.subarray(outPtr, outPtr + prod), 0);
						controller.enqueue(scratch.slice(0, prod)); // one allocation per queued final chunk
					}

					if (code === 1) break;
					if (prod === 0) break;
				}

				// cleanup allocations
				try { if (this._end && this.z) this._end(this.z); } catch (_) { }
				try { if (this.inPtr && this._free) this._free(this.inPtr); } catch (_) { }
				try { if (this.outPtr && this._free) this._free(this.outPtr); } catch (_) { }
			} catch (err) {
				controller.error(err);
			}
		}
	});
}

export class CompressionStreamZlib { constructor(type = "deflate", options) { return _make(true, type, options); } }
export class DecompressionStreamZlib { constructor(type = "deflate", options) { return _make(false, type, options); } }