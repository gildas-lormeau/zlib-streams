# zlib-streams Project

## Overview

This project provides a Compression Streams API in WASM based on zlib and compatible wirg Deflate64 for decompression.

The zlib source code is included as a submodule in `src/zlib`.

## Directory Structure

- `src/` — C source code, including inflate9, zlib, wasm bindings
- `dist/` — WASM build outputs
- `test/` — C test harnesses, payloads, and shell scripts for verification
- `deno/` — Deno/Node.js test scripts and WASM runners

## Getting Started

### 1. Clone the repository and initialize submodules
```sh
git clone <repo-url>
cd project-zlib-streams
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
