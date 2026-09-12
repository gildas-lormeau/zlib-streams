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

## License
See LICENSE for details.
