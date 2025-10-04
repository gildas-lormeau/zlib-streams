/* eslint-disable no-unused-vars */
/* global Buffer, process, TransformStream */

let wasm, malloc, free, memory;

export function setWasmExports(wasmAPI) {
	wasm = wasmAPI;
	({ malloc, free, memory } = wasm);
	if (typeof malloc !== "function" || typeof free !== "function" || !memory) {
		wasm = malloc = free = memory = null;
		throw new Error("Invalid WASM module");
	}
}

function _make(isCompress, type, options = {}) {
	const level = (typeof options.level === "number") ? options.level : -1;
	const outBufferSize = (typeof options.outBuffer === "number") ? options.outBuffer : 64 * 1024;
	const inBufferSize = (typeof options.inBufferSize === "number") ? options.inBufferSize : 64 * 1024;

	return new TransformStream({
		start() {
			let result;
			this._out = malloc(outBufferSize);
			this._in = malloc(inBufferSize);
			this._inBufferSize = inBufferSize;
			this._scratch = new Uint8Array(outBufferSize);
			if (isCompress) {
				this._process = wasm.deflate_process;
				this._last_consumed = wasm.deflate_last_consumed;
				this._end = wasm.deflate_end;
				this._streamHandle = wasm.deflate_new();
				if (type === "gzip") {
					result = wasm.deflate_init_gzip(this._streamHandle, level);
				} else if (type === "deflate-raw") {
					result = wasm.deflate_init_raw(this._streamHandle, level);
				} else {
					result = wasm.deflate_init(this._streamHandle, level);
				}
			} else {
				if (type === "deflate64-raw") {
					this._process = wasm.inflate9_process;
					this._last_consumed = wasm.inflate9_last_consumed;
					this._end = wasm.inflate9_end;
					this._streamHandle = wasm.inflate9_new();
					result = wasm.inflate9_init_raw(this._streamHandle);
				} else {
					this._process = wasm.inflate_process;
					this._last_consumed = wasm.inflate_last_consumed;
					this._end = wasm.inflate_end;
					this._streamHandle = wasm.inflate_new();
					if (type === "deflate-raw") {
						result = wasm.inflate_init_raw(this._streamHandle);
					} else if (type === "gzip") {
						result = wasm.inflate_init_gzip(this._streamHandle);
					} else {
						result = wasm.inflate_init(this._streamHandle);
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
				const heap = new Uint8Array(memory.buffer);
				const process = this._process;
				const last_consumed = this._last_consumed;
				const out = this._out;
				const scratch = this._scratch;
				let offset = 0;
				while (offset < buffer.length) {
					const toRead = Math.min(buffer.length - offset, 32 * 1024);
					if (!this._in || this._inBufferSize < toRead) {
						if (this._in && free) {
							free(this._in);
						}
						this._in = malloc(toRead);
						this._inBufferSize = toRead;
					}
					heap.set(buffer.subarray(offset, offset + toRead), this._in);
					const result = process(this._streamHandle, this._in, toRead, out, outBufferSize, 0);
					if (!isCompress && result < 0) {
						throw new Error("process error:" + result);
					}
					const prod = result & 0x00ffffff;
					if (prod) {
						scratch.set(heap.subarray(out, out + prod), 0);
						controller.enqueue(scratch.slice(0, prod));
					}
					const consumed = last_consumed(this._streamHandle);
					if (consumed === 0) {
						break;
					}
					offset += consumed;
				}
			} catch (error) {
				if (this._end && this._streamHandle) {
					this._end(this._streamHandle);
				}
				if (this._in && free) {
					free(this._in);
				}
				if (this._out && free) {
					free(this._out);
				}
				controller.error(error);
			}
		},
		flush(controller) {
			try {
				const heap = new Uint8Array(memory.buffer);
				const process = this._process;
				const out = this._out;
				const scratch = this._scratch;
				while (true) {
					const result = process(this._streamHandle, 0, 0, out, outBufferSize, 4);
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
				if (this._end && this._streamHandle) {
					const result = this._end(this._streamHandle);
					if (result !== 0) {
						controller.error(new Error("end error:" + result));
					}
				}
				if (this._in && free) {
					free(this._in);
				}
				if (this._out && free) {
					free(this._out);
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