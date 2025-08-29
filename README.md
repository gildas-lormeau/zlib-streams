# zlib-streams Project

## Overview

This project provides Deflate/Deflate64 compression and decompression and streaming tools, with a focus on compatibility, testing, and WebAssembly (WASM) support. It includes:

- C reference implementations for Deflate64 decompression
- Comprehensive test harnesses for verifying decompression correctness
- WASM builds for use in JavaScript/Node.js/Deno environments
- Optional generator for creating Deflate64 ZIPs using the 7-Zip SDK
- Scripts for CI, payload verification, and roundtrip tests

The zlib source code is included as a submodule (`src/zlib`).

## Directory Structure

- `src/` — C source code, including inflate9, zlib, wasm bindings, and 7zip SDK integration
- `test/` — C test harnesses, payloads, and shell scripts for verification
- `deno/` — Deno/Node.js test scripts and WASM runners
- `dist/` — WASM build outputs

## Getting Started

### 1. Clone the repository and initialize submodules
```sh
git clone <repo-url>
cd project-zlib-streams
git submodule update --init --recursive
```

### 2. Build C test harnesses
```sh
make test_all
```
This builds:
- `test/payload_decompress_test`
- `test/payload_decompress_test_debug`
- `test/payload_decompress_ref`
- `test/payload_decompress_ref_debug`

### 3. Run tests
```sh
make run_all_payloads_verify_ci
```
Or run individual scripts in `test/` for more granular testing.

### 4. Build WASM module
```sh
make wasm
```
Output: `dist/zlib_streams.wasm`

### 5. Run WASM/JS tests
```sh
make run_all_tests
```
Or use the Deno/Node.js scripts in `deno/` and `src/wasm/tests/`.

### 6. (Optional) Build the Deflate64 ZIP generator
To build the generator using the 7-Zip SDK:
```sh
make src/7zip
make generator ENABLE_SDK_GENERATOR=1
```

## Notes
- Requires GCC/Clang for C builds, and Emscripten for WASM builds.
- The zlib code is managed as a git submodule in `src/zlib`.
- Build artifacts and SDKs are ignored by git via `.gitignore`.

## License
See LICENSE for details.
