const _bufPool = new Map();
let _defaultWasm = null;

export function setWasmExports(obj) {
	_defaultWasm = obj;
}
export class CompressionStream {
	constructor(type = "deflate", options) {
		return getStreamClass(true, type, options);
	}
}
export class DecompressionStream {
	constructor(type = "deflate", options) {
		return getStreamClass(false, type, options);
	}
}

function getStreamClass(isCompress, type, options = {}) {
	const level = (typeof options.level === "number") ? options.level : -1;
	const OUT = (typeof options.outBuffer === "number") ? options.outBuffer : 64 * 1024;
	const IN_BUF = (typeof options.inBufferSize === "number") ? options.inBufferSize : 64 * 1024;
	const ZERO_COPY = (typeof options.zeroCopyOutput === "boolean") ? options.zeroCopyOutput : true;
	const POOL_MAX = (typeof options.poolMax === "number") ? options.poolMax : 2;
	const FORCE_BUFFER = !!options.forceBuffer;
	const COMPAT_MODE = options.compatMode || "auto";
	const _isNode = (typeof process !== "undefined" && process && process.versions && process.versions.node);
	const WANT_BUFFER = FORCE_BUFFER || (COMPAT_MODE === "buffer") || (COMPAT_MODE === "auto" && _isNode);

	return new TransformStream({
		start(controller) {
			// simple pool for in/out wasm buffer pointers to avoid churn (module-local)
			const poolKey = (OUT + ":" + IN_BUF);
			const getBufPair = () => {
				const pool = _bufPool.get(poolKey) || [];
				if (pool.length) return pool.pop();
				const outPtr = this._malloc(OUT);
				if (!outPtr) return null;
				const inPtr = this._malloc(IN_BUF);
				if (!inPtr) { try { if (outPtr && this._free) this._free(outPtr); } catch (_) { } return null; }
				return { outPtr, inPtr };
			};
			const releaseBufPair = (pair) => {
				if (!pair) return;
				const pool = _bufPool.get(poolKey) || [];
				if (pool.length >= POOL_MAX) {
					// pool is full; free pointers back to wasm if possible to avoid unbounded growth
					try {
						if (pair.inPtr && this._free) { this._free(pair.inPtr); }
					} catch (e) { /* ignore */ }
					try {
						if (pair.outPtr && this._free) { this._free(pair.outPtr); }
					} catch (e) { /* ignore */ }
					return;
				}
				pool.push(pair);
				_bufPool.set(poolKey, pool);
			};
			this.wasm = options.wasm || _defaultWasm || globalThis.WASM_EXPORTS; if (!this.wasm) throw new Error("wasm required");
			this._malloc = this.wasm.malloc;
			this._free = (typeof this.wasm.free === "function") ? this.wasm.free : (typeof this.wasm._free === "function" ? this.wasm._free : null);

			this._cleanup = () => {
				try {
					if (this._end && this.z) {
						try { this._end(this.z); } catch (e) { /* ignore */ }
					}
				} catch (e) { /* ignore */ }
				try {
					if (this._pooled_pair && typeof releaseBufPair === "function") {
						try { releaseBufPair(this._pooled_pair); } catch (_) { /* ignore */ }
					} else {
						try { if (this.inPtr && this._free) { this._free(this.inPtr); } } catch (e) { /* ignore */ }
						try { if (this.outPtr && this._free) { this._free(this.outPtr); } } catch (e) { /* ignore */ }
					}
					this.z = 0; this.inPtr = 0; this.inPtr_sz = 0; this.outPtr = 0;
				} catch (e) { /* ignore */ }
			};

			const pairAlloc = getBufPair();
			if (!pairAlloc) { this._cleanup(); throw new Error("malloc"); }
			this._pooled_pair = pairAlloc;
			this.outPtr = pairAlloc.outPtr;
			this.inPtr = pairAlloc.inPtr;
			this.inPtr_sz = IN_BUF;
			this._heap = new Uint8Array(this.wasm.memory.buffer);
			this._scratch = new Uint8Array(OUT);
			try {
				if (isCompress) {
					this.z = this.wasm.wasm_deflate_new(); if (!this.z) throw new Error("alloc");
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
						this.z = this.wasm.wasm_inflate9_new(); if (!this.z) throw new Error("alloc");
						this.wasm.wasm_inflate9_init_raw(this.z);
						this._process = this.wasm.wasm_inflate9_process;
						this._last_consumed = this.wasm.wasm_inflate9_last_consumed;
						this._end = this.wasm.wasm_inflate9_end;
					} else {
						this.z = this.wasm.wasm_inflate_new(); if (!this.z) throw new Error("alloc");
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
			} catch (e) {
				this._cleanup();
				throw e;
			}
		},

		transform(chunk, controller) {
			try {
				const buf = _norm(chunk);
				let off = 0;
				while (off < buf.length) {
					const toRead = Math.min(buf.length - off, 32 * 1024);
					if (!this.inPtr || this.inPtr_sz < toRead) {
						let need = toRead;
						let newSz = this.inPtr_sz || 1;
						while (newSz < need) newSz *= 2;
						if (this.inPtr && this._free) try { this._free(this.inPtr); } catch (e) { /* ignore */ }
						this.inPtr = this._malloc(newSz); this.inPtr_sz = newSz; if (!this.inPtr) throw new Error("malloc");
					}

					if (!this._heap || this._heap.buffer !== this.wasm.memory.buffer) this._heap = new Uint8Array(this.wasm.memory.buffer);
					const HEAP = this._heap;
					HEAP.set(buf.subarray(off, off + toRead), this.inPtr);

					const ret = this._process(this.z, this.inPtr, toRead, this.outPtr, OUT, 0);

					if (!isCompress && ret < 0) throw new Error("inflate error:" + ret);

					const prod = ret & 0x00ffffff;
					if (prod) {
						this._scratch.set(HEAP.subarray(this.outPtr, this.outPtr + prod), 0);
						const outView = this._scratch.subarray(0, prod);
						if (WANT_BUFFER) {
							if (typeof Buffer !== "undefined" && typeof Buffer.from === "function") controller.enqueue(Buffer.from(outView));
							else controller.enqueue(outView);
						} else if (ZERO_COPY) {
							controller.enqueue(outView);
						} else if (typeof Buffer !== "undefined" && typeof Buffer.from === "function") {
							controller.enqueue(Buffer.from(outView));
						} else {
							controller.enqueue(outView);
						}
					}

					const consumed = (this._last_consumed) ? this._last_consumed(this.z) : 0;
					if (consumed === 0) break;
					off += consumed;
				}
			} catch (e) { try { if (this._cleanup) this._cleanup(); } catch (_) { } controller.error(e); }
		},

		flush(controller) {
			try {
				if (!this._heap || this._heap.buffer !== this.wasm.memory.buffer) this._heap = new Uint8Array(this.wasm.memory.buffer);
				const HEAP = this._heap;
				while (true) {
					const ret = this._process(this.z, 0, 0, this.outPtr, OUT, 4);
					if (!isCompress && ret < 0) throw new Error("inflate error:" + ret);
					const prod = ret & 0x00ffffff;
					const code = (ret >> 24) & 0xff;
					if (prod) {
						this._scratch.set(HEAP.subarray(this.outPtr, this.outPtr + prod), 0);
						const outView = this._scratch.subarray(0, prod);
						if (WANT_BUFFER) {
							if (typeof Buffer !== "undefined" && typeof Buffer.from === "function") controller.enqueue(Buffer.from(outView));
							else controller.enqueue(outView);
						} else if (ZERO_COPY) {
							controller.enqueue(outView);
						} else if (typeof Buffer !== "undefined" && typeof Buffer.from === "function") {
							controller.enqueue(Buffer.from(outView));
						} else {
							controller.enqueue(outView);
						}
					}
					if (code === 1) break;
					if (prod === 0) break;
				}
				if (this._cleanup) try { this._cleanup(); } catch (e) { /* ignore */ }
			} catch (e) {
				controller.error(e);
			}
		}
	});
}

function _norm(chunk) {
	if (chunk instanceof Uint8Array) return chunk;
	if (ArrayBuffer.isView(chunk)) return new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength);
	if (chunk instanceof ArrayBuffer) return new Uint8Array(chunk);
	if (typeof Buffer !== "undefined" && Buffer.isBuffer(chunk)) return new Uint8Array(chunk);
	throw new TypeError("unsupported chunk");
}