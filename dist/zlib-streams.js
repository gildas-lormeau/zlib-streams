/* eslint-disable no-unused-vars */
/* global Buffer, process, TransformStream */

let wasm, malloc, free, memory;

export function setWasmExports(wasmAPI) {
	wasm = wasmAPI;
	({ malloc, free, memory } = wasm);
}

function _make(isCompress, type, options = {}) {
	const level = (typeof options.level === "number") ? options.level : -1;
	const outBufferSize = (typeof options.outBuffer === "number") ? options.outBuffer : 64 * 1024;
	const inBufferSize = (typeof options.inBufferSize === "number") ? options.inBufferSize : 64 * 1024;

	return new TransformStream({
		start() {
			let result;
			this.out = malloc(outBufferSize);
			this.in = malloc(inBufferSize);
			this.inBufferSize = inBufferSize;
			this._heap = new Uint8Array(memory.buffer);
			this._scratch = new Uint8Array(outBufferSize);
			if (isCompress) {
				this._process = wasm.deflate_process;
				this._last_consumed = wasm.deflate_last_consumed;
				this._end = wasm.deflate_end;
				this.streamHandle = wasm.deflate_new();
				if (type === "gzip") {
					result = level >= 0 ?
						wasm.deflate_init_gzip_level(this.streamHandle, level) :
						wasm.deflate_init_gzip(this.streamHandle);
				} else if (type === "deflate-raw") {
					result = level >= 0 ?
						wasm.deflate_init_raw_level(this.streamHandle, level) :
						wasm.deflate_init_raw(this.streamHandle);
				} else {
					result = level >= 0 ?
						wasm.deflate_init_level(this.streamHandle, level) :
						wasm.deflate_init(this.streamHandle);
				}
			} else {
				if (type === "deflate64-raw") {
					this._process = wasm.inflate9_process;
					this._last_consumed = wasm.inflate9_last_consumed;
					this._end = wasm.inflate9_end;
					this.streamHandle = wasm.inflate9_new();
					result = wasm.inflate9_init_raw(this.streamHandle);
				} else {
					this._process = wasm.inflate_process;
					this._last_consumed = wasm.inflate_last_consumed;
					this._end = wasm.inflate_end;
					this.streamHandle = wasm.inflate_new();
					if (type === "deflate-raw") {
						result = wasm.inflate_init_raw(this.streamHandle);
					} else if (type === "gzip") {
						result = wasm.inflate_init_gzip(this.streamHandle);
					} else {
						result = wasm.inflate_init(this.streamHandle);
					}
				}
			}
			if (result !== 0) {
				throw new Error("init failed:" + result);
			}
		},
		transform(chunk, controller) {
			try {
				const buffer = chunk;
				const heap = this._heap;
				const process = this._process;
				const last_consumed = this._last_consumed;
				const out = this.out;
				const scratch = this._scratch;
				let offset = 0;
				while (offset < buffer.length) {
					const toRead = Math.min(buffer.length - offset, 32 * 1024);
					if (!this.in || this.inBufferSize < toRead) {
						if (this.in && free) {
							free(this.in);
						}
						this.in = malloc(toRead);
						this.inBufferSize = toRead;
					}
					heap.set(buffer.subarray(offset, offset + toRead), this.in);
					const result = process(this.streamHandle, this.in, toRead, out, outBufferSize, 0);
					if (!isCompress && result < 0) {
						throw new Error("process error:" + result);
					}
					const prod = result & 0x00ffffff;
					if (prod) {
						scratch.set(heap.subarray(out, out + prod), 0);
						controller.enqueue(scratch.slice(0, prod));
					}
					const consumed = last_consumed(this.streamHandle);
					if (consumed === 0) {
						break;
					}
					offset += consumed;
				}
			} catch (error) {
				if (this._end && this.streamHandle) {
					this._end(this.streamHandle);
				}
				if (this.in && free) {
					free(this.in);
				}
				if (this.out && free) {
					free(this.out);
				}
				controller.error(error);
			}
		},
		flush(controller) {
			try {
				const heap = this._heap;
				const process = this._process;
				const out = this.out;
				const scratch = this._scratch;
				while (true) {
					const result = process(this.streamHandle, 0, 0, out, outBufferSize, 4);
					if (!isCompress && result < 0) {
						throw new Error("process error:" + result);
					}
					const produced = result & 0x00ffffff;
					const code = (result >> 24) & 0xff;
					if (produced) {
						scratch.set(heap.subarray(out, out + produced), 0);
						controller.enqueue(scratch.slice(0, produced));
					}
					if (code === 1 || produced === 0) {
						break;
					}
				}
			} catch (error) {
				controller.error(error);
			} finally {
				if (this._end && this.streamHandle) {
					const result = this._end(this.streamHandle);
					if (result !== 0) {
						controller.error(new Error("end error:" + result));
					}
				}
				if (this.in && free) {
					free(this.in);
				}
				if (this.out && free) {
					free(this.out);
				}
			}
		}
	});
}

export class CompressionStreamZlib {
	constructor(type = "deflate", options) {
		return _make(true, type, options);
	}
}
export class DecompressionStreamZlib {
	constructor(type = "deflate", options) {
		return _make(false, type, options);
	}
}