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
- Developmeent
```sh
make wasm
```
Output: `dist/zlib-streams-dev.wasm` and `dist/zlib-streams.js`

- Production
```sh
make wasm_prod
```
Output: `dist/zlib-streams.wasm` and `dist/zlib-streams.js`

## License
See LICENSE for details.
