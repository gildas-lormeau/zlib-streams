# WASM-based Compression Streams API implementation using zlib, with support for deflate64 decompression

The zlib source code is included as a submodule in `src/zlib`.

## Directory Structure

- `src/` — C source code, including inflate9, zlib, wasm bindings
- `dist/` — WASM build outputs
- `test/` — C test harnesses, payloads, and shell scripts for verification
- `deno/` — Deno/Node.js test scripts and WASM runners

## Getting Started

### 1. Clone the repository and initialize submodules
```sh
git clone https://github.com/gildas-lormeau/zlib-streams.git
cd zlib-streams
git submodule update --init --recursive
```

### 2. Build WASM module
The build needs the Emscripten SDK. The Makefile looks for it in `emsdk/` at the repository root (ignored by git) and can be pointed elsewhere with `make EMCC=... WASM_OPT=...`.

- Development: `-O2`, used by the test targets. Do not benchmark or ship this file.
```sh
make wasm
```
Output: `dist/zlib-streams-dev.wasm` and `dist/zlib-streams.js`

- Production: `-Oz` plus `wasm-opt`. This is the published module.
```sh
make wasm_prod
```
Output: `dist/zlib-streams.wasm` and `dist/zlib-streams.js`

- zip.js module: the codecs plus the AES-CTR/HMAC-SHA1 engine behind the encrypted entries of zip.js, vendored by zip.js as `lib/core/streams/zlib-wasm/zlib-streams.wasm`.
```sh
make zip_module
make test_zip_module
```
Output: `dist/zip-module.wasm`

The files in `dist/` are tracked, so a checkout leaves them with the same timestamp as the sources and `make dist/<file>` reports them up to date even when the sources changed. The three targets above always rebuild their file; the test targets depend on the file rules and rebuild only when a source is newer. Rebuild and commit `dist/` in the same commit as a change to `src/`.

## API

The npm package exports the JavaScript driver as `zlib-streams/zlib-streams.js` and the modules as `zlib-streams/zlib-streams.wasm` and `zlib-streams/zip-module.wasm`. The module imports nothing, so it is instantiated bare, and its exports are handed to the driver before any stream is created.

```js
import { setWasmExports, CompressionStreamZlib, DecompressionStreamZlib } from "zlib-streams/zlib-streams.js";

const wasmBytes = await (await fetch(import.meta.resolve("zlib-streams/zlib-streams.wasm"))).arrayBuffer();
const { instance } = await WebAssembly.instantiate(wasmBytes);
setWasmExports(instance.exports);

const compressed = new Blob(["Hello World"]).stream().pipeThrough(new CompressionStreamZlib("gzip", { level: 9 }));
const text = await new Response(compressed.pipeThrough(new DecompressionStreamZlib("gzip"))).text();
```

### Driver functions

- `setWasmExports(exports)`: registers the exports of an instantiated module (`malloc`, `free`, `memory` and the codec functions); throws `"Invalid WASM module"` when they are missing.
- `resetWasmExports()`: forgets the exports; the next stream throws `"WASM module not loaded"`.
- `setInitError(error)`: records why the module could not be loaded; the error is the `cause` of `"WASM module not loaded"`.

### Streams

```js
new CompressionStreamZlib(format = "deflate", { level, outBuffer, inBufferSize })
new DecompressionStreamZlib(format = "deflate", { outBuffer, inBufferSize })
```

Both classes return a `{ readable, writable }` pair usable with `pipeThrough()`. The formats are `"deflate"` (the zlib wrapper, RFC 1950), `"deflate-raw"` (RFC 1951, no wrapper), `"gzip"` and, for decompression only, `"deflate64-raw"`. `level` is the zlib compression level, 0 to 9, -1 (the default) for the zlib default. `outBuffer` and `inBufferSize` are the sizes in bytes of the output and input buffers allocated in the module heap, 64 KB each by default; input chunks are processed in slices of at most 32 KB.

The static properties `supportedFormats` (an array) and `requiresModule` (`true`) let a host check the formats and know that the classes are unusable until `setWasmExports()` has run.

### Errors

- Bytes following the end of a stream are rejected with `"trailing data after the end of the stream"`, whatever the format, as the `DecompressionStream` of the browsers does; concatenated gzip members are not decoded.
- A corrupted stream fails with `"process error:<code>"` carrying the zlib return code; a stream whose end is missing fails on close with `"end error:<code>"`.
- A failure to allocate memory, on construction or while processing, carries the property `code` set to `"Z_MEM_ERROR"`: `"allocation failed"` when the heap is exhausted, `"init failed:-4"` or `"process error:-4"` when zlib reports it.

The heap of the module is fixed, so the number of streams alive at the same time is bounded by their buffers; a stream releases its buffers when it closes, errors or is cancelled.

## License
See LICENSE for details.
