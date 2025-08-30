CC=gcc
CFLAGS=-I./src/zlib -I./src -I./test -I./src/zlib/contrib/infback9 -O2 -w -MMD -MP
DEBUG_CFLAGS=-I./src/zlib -I./src -I./test -I./src/zlib/contrib/infback9 -O0 -g3 -ggdb3 -fno-inline -fno-omit-frame-pointer -w -MMD -MP
## By default we don't enable the INF9 tracing macros so debug binaries
## don't embed large diagnostic strings. To enable tracing for local
## debugging, uncomment and set DEBUG_DEFINES accordingly.
# Enable zlib's Tracev/Tracevv debug output and inf9 trace hooks for debug builds
# For traced wasm runs we enable INF9_TRACE via DEBUG_DEFINES_TRACED (used by the traced-wasm target)
## Debug defines are disabled by default for a clean commit-ready Makefile.
DEBUG_DEFINES=
DEBUG_DEFINES_TRACED=
SRC=src/inflate9.c src/zlib/contrib/infback9/inftree9.c src/zlib/zutil.c src/zlib/inflate.c src/zlib/inftrees.c
TESTSRC=zlib/crc32.c
LIBS=-lz

## Build the payload decompressor test binary used for verification (deduplicated sources)
PD_SRCS = test/payload_decompress.c \
	src/inflate9.c src/zlib/inffast.c src/zlib/inftrees.c src/zlib/zutil.c src/zlib/inflate.c src/zlib/infback.c \
	src/zlib/contrib/infback9/infback9.c src/zlib/contrib/infback9/inftree9.c src/zlib/crc32.c src/zlib/adler32.c src/zlib/trees.c

PD_NOWINDOW_SRCS = test/payload_decompress_nowindow.c \
	src/inflate9.c src/zlib/inffast.c src/zlib/inftrees.c src/zlib/zutil.c src/zlib/inflate.c src/zlib/infback.c \
	src/zlib/contrib/infback9/infback9.c src/zlib/contrib/infback9/inftree9.c src/zlib/crc32.c src/zlib/adler32.c src/zlib/trees.c

PD_NOWINDOW_DEBUG_OBJS = $(PD_NOWINDOW_SRCS:%.c=build/debug/%.o)

# Dedicated reference test that uses inflateBack9 (infback9.c) directly
PD_REF_SRCS = test/payload_decompress_ref.c \
	src/zlib/contrib/infback9/infback9.c src/zlib/contrib/infback9/inftree9.c src/zlib/crc32.c src/zlib/adler32.c src/zlib/trees.c src/zlib/zutil.c

PD_REF_OBJS = $(PD_REF_SRCS:%.c=build/debug/%.o)
PD_REF_OBJS_RELEASE = $(PD_REF_SRCS:%.c=build/%.o)

# object paths for release and debug builds
PD_OBJS = $(PD_SRCS:%.c=build/%.o)
PD_DEBUG_OBJS = $(PD_SRCS:%.c=build/debug/%.o)

all: test_all

clean:
	@echo "Cleaning build artifacts, dist, tmp, and generated files"
	rm -rf ./test/payload_decompress_test_debug ./test/payload_decompress_ref_debug ./test/payload_decompress_test_debug.* ./test/payload_decompress_ref_debug.* build tmp *.d dist/*.wasm tmp/all_runs tmp/run_all_verify.log
	# remove node generated artifacts if present
	rm -f src/wasm/tests/*.out || true

test/payload_decompress_test: $(PD_OBJS)
	mkdir -p test
	$(CC) $(CFLAGS) $(PD_OBJS) -o $@

# Debug variant of the payload decompressor (with debug symbols)
test_all: test/payload_decompress_test test/payload_decompress_test_debug test/payload_decompress_ref test/payload_decompress_ref_debug
	@echo "Built test binaries: test/payload_decompress_test test/payload_decompress_test_debug test/payload_decompress_ref test/payload_decompress_ref_debug"

# Build-only debug target used by IDEs (compile only, do not run)
test/payload_decompress_test_debug: $(PD_DEBUG_OBJS)
	mkdir -p test
	$(CC) $(DEBUG_DEFINES) $(DEBUG_CFLAGS) $(PD_DEBUG_OBJS) -o $@
	@echo "Debug binary built: $@"

# Build a true reference binary that uses inflateBack9 (infback9.c)
test/payload_decompress_ref_debug: $(PD_REF_OBJS)
	mkdir -p test
	$(CC) $(DEBUG_DEFINES) $(DEBUG_CFLAGS) $(PD_REF_OBJS) -o $@

# Build nowindow debug variant
test/payload_decompress_nowindow_debug: $(PD_NOWINDOW_DEBUG_OBJS)
	mkdir -p test
	$(CC) $(DEBUG_DEFINES) $(DEBUG_CFLAGS) $(PD_NOWINDOW_DEBUG_OBJS) -o $@

test/payload_decompress_ref: $(PD_REF_OBJS_RELEASE)
	mkdir -p test
	$(CC) $(CFLAGS) $(PD_REF_OBJS_RELEASE) -o $@

# pattern rules to compile sources into build object dirs
build/%.o: %.c
	@echo "CC $< -> $@"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

build/debug/%.o: %.c
	@echo "CC (debug) $< -> $@"
	@mkdir -p $(dir $@)
	$(CC) $(DEBUG_DEFINES) $(DEBUG_CFLAGS) -c $< -o $@

run_ref_test:
	@mkdir -p tmp
	@sh -c '\
	./test/payload_decompress_ref_debug ./test/ref-data/10k_lines.deflate64 tmp/out_ref.bin 2> tmp/trace_ref.log; \
	rc=$$?; \
	if [ $$rc -eq 0 ] && [ -s tmp/out_ref.bin ]; then \
		echo OK >> tmp/trace_ref.log; \
	else \
		echo KO >> tmp/trace_ref.log; \
	fi'

.PHONY: test_all ref_test run_ref_test

.PHONY: run_all_payloads_verify_ci
run_all_payloads_verify_ci:
	@echo "Building quiet debug harnesses (no INF9_TRACE) and running full verification..."
	@$(MAKE) DEBUG_DEFINES= test/payload_decompress_test_debug test/payload_decompress_ref_debug test/payload_decompress_nowindow_debug >/dev/null 2>&1 || true
	@mkdir -p tmp/all_runs
	@./test/run_all_payloads_and_verify.sh | tee tmp/all_runs/run_all_verify.log
	@echo "wrote tmp/all_runs/run_all_verify.log"

.PHONY: ci

ci:
	@mkdir -p tmp
	@echo "CI START: $$(date)" > tmp/ci_run.log
	@echo "Running make test (build)" >> tmp/ci_run.log
	@$(MAKE) test_all >> tmp/ci_run.log 2>&1 || true
	@for target run_ref_test; do \
		echo "=== $$target ===" >> tmp/ci_run.log; \
		$(MAKE) $$target >> tmp/ci_run.log 2>&1 || true; \
	done
	@echo "CI DONE: $$(date)" >> tmp/ci_run.log
	@echo "Summary generated at $$(date)" > tmp/ci_summary.txt
	@for out in tmp/out_*.bin; do [ -f "$$out" ] && stat -f"%N %z" "$$out" >> tmp/ci_summary.txt || true; done
	@echo "" >> tmp/ci_summary.txt
	@echo "-- last 10 lines of traces --" >> tmp/ci_summary.txt
	@for trace in tmp/trace_*.log; do echo "--- $$trace ---" >> tmp/ci_summary.txt; tail -n 10 "$$trace" >> tmp/ci_summary.txt 2>/dev/null || true; echo "" >> tmp/ci_summary.txt; done

# Include generated dependency files (if present)
-include $(PD_OBJS:.o=.d) $(PD_DEBUG_OBJS:.o=.d)

# -----------------------------------------------------------------------------
# WASM build target (convenience target to produce dist/zlib-streams-dev.wasm)
# -----------------------------------------------------------------------------
EMCC ?= emsdk/upstream/emscripten/emcc

WASM_SRCS = src/wasm/inflate9_stream_wasm.c src/wasm/inflate_stream_wasm.c src/wasm/deflate_stream_wasm.c src/inflate9.c \
	src/zlib/contrib/infback9/inftree9.c \
	src/zlib/inffast.c src/zlib/inflate.c src/zlib/inftrees.c src/zlib/zutil.c \
	src/zlib/crc32.c src/zlib/adler32.c src/zlib/trees.c src/zlib/deflate.c
WASM_CFLAGS = -Isrc -Isrc/zlib -Isrc/zlib/contrib/infback9 -O2 -flto

.PHONY: wasm
wasm: dist/zlib-streams-dev.wasm

.PHONY: wasm_traced
wasm_traced: dist/zlib-streams_traced.wasm

dist/zlib-streams_traced.wasm: $(WASM_SRCS)
	@echo "Building traced $@ using $(EMCC)"
	@mkdir -p dist
	$(EMCC) $(WASM_SRCS) $(WASM_CFLAGS) $(DEBUG_DEFINES_TRACED) -s WASM=1 -s STANDALONE_WASM=1 --no-entry \
	-s EXPORTED_FUNCTIONS='["_inflate9_new","_inflate9_init","_inflate9_init_raw","_inflate9_process","_inflate9_end","_inflate9_last_consumed","_inflate_new","_inflate_init","_inflate_init_raw","_inflate_init_gzip","_inflate_process","_inflate_end","_inflate_last_consumed","_deflate_new","_deflate_init","_deflate_init_raw","_deflate_init_level","_deflate_init_raw_level","_deflate_init_gzip","_deflate_init_gzip_level","_deflate_process","_deflate_end","_deflate_last_consumed","_malloc","_free"]' \
		-o $@

	# Run reference C and WASM test suites over payloads in test/ref-data
.PHONY: run_ref_c_tests
run_ref_c_tests: test/payload_decompress_test_debug test/payload_decompress_ref_debug test/payload_decompress_nowindow_debug
	@echo "Running C reference harnesses over deflate64 payloads"
	@mkdir -p tmp/all_runs
	@./test/run_deflate64_suite.sh

.PHONY: run_ref_wasm_tests
run_ref_wasm_tests: dist/zlib-streams-dev.wasm
	@echo "Running WASM runner over deflate64 payloads (sequential by default)"
	@mkdir -p tmp/all_runs
	@$(MAKE) run_ref_wasm_seq
	@$(MAKE) run_wasm_roundtrip_mem

.PHONY: run_ref_wasm_seq
run_ref_wasm_seq: dist/zlib-streams-dev.wasm
	@echo "Running WASM runner sequentially per payload (safe, non-blocking)"
	@mkdir -p tmp/all_runs
	@sh -c '\
	for p in test/ref-data/*deflate64*; do \
		[ -f "$$p" ] || continue; \
		f=$$(basename "$$p"); \
		printf "\nPayload: %s\n" "$$f"; \
		rm -f tmp/all_runs/wasm__$$f.out; \
		node src/wasm/tests/test_inflate9_stream.js dist/zlib-streams-dev.wasm "$$p" tmp/all_runs/wasm__$$f.out || printf "  NODE_RC:%s for %s\n" "$$?" "$$f"; \
		if [ -f tmp/all_runs/wasm__$$f.out ]; then \
			printf "  wrote tmp/all_runs/wasm__%s.out (size=%s)\n" "$$f" "$(stat -f%z tmp/all_runs/wasm__$$f.out 2>/dev/null || echo unknown)"; \
			ref=tmp/all_runs/payload_decompress_ref_debug__$$f.out; \
			if [ -f "$$ref" ]; then \
				h1=`shasum -a 256 tmp/all_runs/wasm__$$f.out | cut -d\  -f1`; \
				h2=`shasum -a 256 "$$ref" | cut -d\  -f1`; \
				if [ "$$h1" = "$$h2" ]; then \
					printf "  SHA256 MATCH: %s\n" "$$h1"; \
				else \
					printf "  SHA256 MISMATCH: wasm=%s ref=%s\n" "$$h1" "$$h2"; \
				fi; \
			else \
				printf "  ref missing: %s\n" "$$ref"; \
			fi; \
		fi; \
	done'

.PHONY: run_ref_tests
run_ref_tests: run_ref_c_tests run_ref_wasm_tests
	@echo "Completed C+WASM reference test suites"

.PHONY: run_wasm_roundtrip
run_wasm_roundtrip: dist/zlib-streams-dev.wasm
	@echo "Running wasm roundtrip tests over tmp/all_runs/roundtrip_input*"
	@mkdir -p tmp/all_runs
	# populate tmp/all_runs with canonical roundtrip inputs
	@cp -f test/ref-data/roundtrip_inputs/roundtrip_input.txt tmp/all_runs/roundtrip_input.txt
	@cp -f test/ref-data/roundtrip_inputs/roundtrip_input1.txt tmp/all_runs/roundtrip_input1.txt
	@cp -f test/ref-data/roundtrip_inputs/roundtrip_input3.bin tmp/all_runs/roundtrip_input3.bin
	@sh -c '\
for p in tmp/all_runs/roundtrip_input*; do \
		[ -f "$$p" ] || continue; \
		f=$$(basename "$$p"); \
		printf "\nInput: %s\n" "$$f"; \
		rm -f tmp/all_runs/roundtrip_out__$$f; \
		node src/wasm/tests/test_round_trip_stream.js dist/zlib-streams-dev.wasm "$$p" tmp/all_runs/roundtrip_out__$$f 2>&1 | sed -n '1,200p'; \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			printf "  OK: %s\n" "$$f"; \
		else \
			printf "  FAIL (rc=%s): %s\n" "$$rc" "$$f"; \
		fi; \
	done'

.PHONY: run_wasm_roundtrip_mem
run_wasm_roundtrip_mem: dist/zlib-streams-dev.wasm
	@echo "Running wasm roundtrip (in-memory) tests over tmp/all_runs/roundtrip_input*"
	@mkdir -p tmp/all_runs
	@sh -c '\
	for p in tmp/all_runs/roundtrip_input*; do \
		[ -f "$$p" ] || continue; \
		f=$$(basename "$$p"); \
		printf "\nInput: %s\n" "$$f"; \
		node src/wasm/tests/test_round_trip_stream_mem.js dist/zlib-streams-dev.wasm "$$p" 2>&1 | sed -n "1,200p"; \
		rc=$$?; \
		if [ $$rc -eq 0 ]; then \
			printf "  OK: %s\n" "$$f"; \
		else \
			printf "  FAIL (rc=%s): %s\n" "$$rc" "$$f"; \
		fi; \
	done'

.PHONY: run_transform_roundtrip
run_transform_roundtrip: dist/zlib-streams-dev.wasm
	@echo "Running TransformStream roundtrip test"
	@node src/wasm/tests/test_transform_roundtrip.js dist/zlib-streams-dev.wasm

.PHONY: deno_run_tests
deno_run_tests:
	@echo "Running Deno test suite (deno/run_all_tests.sh)"
	@chmod +x deno/run_all_tests.sh || true
	@./deno/run_all_tests.sh dist/zlib-streams.wasm

.PHONY: run_inflate9_roundtrip_all
run_inflate9_roundtrip_all: dist/zlib-streams-dev.wasm
	@echo "Running inflate9 roundtrip over deflate64 payloads"
	@node src/wasm/tests/test_inflate9_roundtrip_all.js dist/zlib-streams-dev.wasm

.PHONY: test_decompressionstream_inflate9
test_decompressionstream_inflate9: dist/zlib-streams-dev.wasm
	@echo "Testing DecompressionStreamZlib against native inflate9 for deflate64 payloads"
	@node src/wasm/tests/test_decompressionstream_inflate9.js dist/zlib-streams-dev.wasm


# Single target to run the main wasm/TransformStream tests used during development
.PHONY: run_all_tests
run_all_tests: dist/zlib-streams-dev.wasm
	@echo "Running all wasm TransformStream tests"
	@$(MAKE) run_ref_wasm_tests
	@$(MAKE) run_wasm_roundtrip
	# Additional roundtrip tests not covered by the generic runners
	@node src/wasm/tests/test_round_trip_stream_deflate.js dist/zlib-streams-dev.wasm
	@node src/wasm/tests/test_round_trip_stream_gzip.js dist/zlib-streams-dev.wasm
	@mkdir -p tmp/all_runs
	# Ensure a raw-deflate test input exists (create from a repeated pattern)
	@node -e "const fs=require('fs'), z=require('zlib'); fs.writeFileSync('tmp/all_runs/roundtrip_input3.bin', z.deflateRawSync(Buffer.alloc(24000, 'x')));"
	@node src/wasm/tests/test_inflate_stream.js dist/zlib-streams-dev.wasm tmp/all_runs/roundtrip_input3.bin tmp/all_runs/inflate_stream_out.bin
	# CLI helper run (small sample) for regressions/throughput checks
	@node src/wasm/tests/run_roundtrip_cli.js deflate 24000 dist/zlib-streams-dev.wasm
	@$(MAKE) run_transform_roundtrip
	@$(MAKE) test_decompressionstream_inflate9
	@echo "Completed run_all_tests"

dist/zlib-streams-dev.wasm: $(WASM_SRCS)
	@echo "Building $@ using $(EMCC)"
	@mkdir -p dist
	$(EMCC) $(WASM_SRCS) $(WASM_CFLAGS) -s WASM=1 -s STANDALONE_WASM=1 --no-entry \
		-s EXPORTED_FUNCTIONS='["_inflate9_new","_inflate9_init","_inflate9_init_raw","_inflate9_process","_inflate9_end","_inflate9_last_consumed","_inflate_new","_inflate_init","_inflate_init_raw","_inflate_init_gzip","_inflate_process","_inflate_end","_inflate_last_consumed","_deflate_new","_deflate_init","_deflate_init_raw","_deflate_init_level","_deflate_init_raw_level","_deflate_init_gzip","_deflate_init_gzip_level","_deflate_process","_deflate_end","_deflate_last_consumed","_malloc","_free"]' \
		-o $@
	cp src/wasm/api/zlib-streams.js dist/zlib-streams.js

# Production-optimized wasm: smaller build with -Oz and no extra runtime methods.
.PHONY: wasm_prod
wasm_prod: dist/zlib-streams.wasm

dist/zlib-streams.wasm: $(WASM_SRCS)
	@echo "Building production wasm $@ using $(EMCC)"
	@mkdir -p dist
	$(EMCC) $(WASM_SRCS) $(WASM_CFLAGS) -Oz -flto -s WASM=1 -s STANDALONE_WASM=1 --no-entry \
		-s FILESYSTEM=0 -s DISABLE_EXCEPTION_CATCHING=1 \
		-s EXPORTED_FUNCTIONS='["_inflate9_new","_inflate9_init","_inflate9_init_raw","_inflate9_process","_inflate9_end","_inflate9_last_consumed","_inflate_new","_inflate_init","_inflate_init_raw","_inflate_init_gzip","_inflate_process","_inflate_end","_inflate_last_consumed","_deflate_new","_deflate_init","_deflate_init_raw","_deflate_init_level","_deflate_init_raw_level","_deflate_init_gzip","_deflate_init_gzip_level","_deflate_process","_deflate_end","_deflate_last_consumed","_malloc","_free"]' \
		-o $@
	cp src/wasm/api/zlib-streams.js dist/zlib-streams.js
	@which wasm-opt >/dev/null 2>&1 && { echo "Running wasm-opt -Oz --enable-bulk-memory-opt"; wasm-opt -Oz --enable-bulk-memory-opt -o $@ $@ || true; } || true

# -----------------------------------------------------------------------------
# Optional generator: build a C++ tool that creates Deflate64 ZIPs using the
# 7-Zip SDK. This target is guarded: it only compiles when the SDK is present
# at src/7zip. The generator is not built by default.
# -----------------------------------------------------------------------------
CXX ?= g++
CXXFLAGS = -std=c++17 -I./src -I./src/7zip -O2

SDK_ROOT = src/7zip/CPP/7zip
# Guard building the SDK-backed generator. Set ENABLE_SDK_GENERATOR=1 to
# attempt compilation. By default, we skip to avoid platform-specific SDK
# build errors on non-Windows hosts.
ENABLE_SDK_GENERATOR ?= 0

# Only require the heavy SDK archive when we actually plan to build the
# SDK-backed generator. This avoids building the SDK object archive when
# the generator is being built in CLI-only mode.
SDK_PREREQS := $(if $(filter 1,$(ENABLE_SDK_GENERATOR)),build/lib7zip.a,)

# Minimal curated list of SDK sources required to build a Zip creator that
# uses compress/deflate/deflate64. This avoids compiling UI/Explorer/Windows
# specific code which fails on non-Windows hosts. Add more files here if
# the linker reports missing symbols.
SDK_FAST_SRCS = \
	$(SDK_ROOT)/Archive/Zip/ZipAddCommon.cpp \
	$(SDK_ROOT)/Archive/Zip/ZipHandler.cpp \
	$(SDK_ROOT)/Archive/Zip/ZipHandlerOut.cpp \
	$(SDK_ROOT)/Archive/Zip/ZipIn.cpp \
	$(SDK_ROOT)/Archive/Zip/ZipItem.cpp \
	$(SDK_ROOT)/Archive/Zip/ZipOut.cpp \
	$(SDK_ROOT)/Archive/Zip/ZipRegister.cpp \
	$(SDK_ROOT)/Archive/Zip/ZipUpdate.cpp \
	$(SDK_ROOT)/Common/CreateCoder.cpp \
	$(SDK_ROOT)/../Common/MyString.cpp \
	$(SDK_ROOT)/../Common/StringConvert.cpp \
	$(SDK_ROOT)/../Common/UTFConvert.cpp \
	$(SDK_ROOT)/../Common/IntToString.cpp \
	$(SDK_ROOT)/../Common/MyWindows.cpp \
	$(SDK_ROOT)/Common/StreamObjects.cpp \
	$(SDK_ROOT)/Common/OutMemStream.cpp \
	$(SDK_ROOT)/Common/OutBuffer.cpp \
	$(SDK_ROOT)/Common/InBuffer.cpp \
	$(SDK_ROOT)/Common/InOutTempBuffer.cpp \
	$(SDK_ROOT)/Common/StreamUtils.cpp \
	$(SDK_ROOT)/Common/OffsetStream.cpp \
	$(SDK_ROOT)/Common/FileStreams.cpp \
	$(SDK_ROOT)/Common/CWrappers.cpp \
	$(SDK_ROOT)/Common/FilterCoder.cpp \
	$(SDK_ROOT)/Compress/CopyCoder.cpp \
	$(SDK_ROOT)/Compress/DeflateRegister.cpp \
	$(SDK_ROOT)/Compress/DeflateEncoder.cpp \
	$(SDK_ROOT)/Compress/DeflateDecoder.cpp \
	$(SDK_ROOT)/Compress/LzOutWindow.cpp \
	$(SDK_ROOT)/Compress/Deflate64Register.cpp \
	$(SDK_ROOT)/../Windows/FileIO.cpp \
	$(SDK_ROOT)/../Windows/FileFind.cpp \
	$(SDK_ROOT)/../Windows/FileName.cpp \
	$(SDK_ROOT)/../Windows/FileMapping.cpp \
	$(SDK_ROOT)/../Windows/System.cpp \
	$(SDK_ROOT)/../Windows/TimeUtils.cpp \
	$(SDK_ROOT)/../Windows/PropVariant.cpp \
	$(SDK_ROOT)/../Windows/PropVariantConv.cpp \
	$(SDK_ROOT)/../Windows/MemoryGlobal.cpp \
	$(SDK_ROOT)/../Windows/MemoryLock.cpp \
	$(SDK_ROOT)/../Windows/SystemInfo.cpp

SDK_C_SRCS := $(wildcard src/7zip/C/*.c)

SDK_C_OBJS := $(patsubst src/7zip/C/%.c,build/7zip/C/%.o,$(SDK_C_SRCS))

SDK_CPP := $(shell [ -d src/7zip ] && [ "$(ENABLE_SDK_GENERATOR)" -eq "1" ] && echo $(SDK_FAST_SRCS) || echo)
SDK_OBJS := $(patsubst $(SDK_ROOT)/%.cpp,build/7zip/%.o,$(SDK_CPP))

# Any extra non-SDK .cpp sources that should be compiled into the SDK archive
SDK_EXTRA_SRCS := src/generator/sdk_deflate64.cpp \
	src/generator/win_compat.cpp \
	src/generator/com_shims.cpp \
	src/generator/sdk_shims.cpp
SDK_EXTRA_OBJS := $(patsubst src/%.cpp,build/7zip/%.o,$(SDK_EXTRA_SRCS))

build/lib7zip.a: $(SDK_OBJS) $(SDK_C_OBJS) $(SDK_EXTRA_OBJS)
	@echo "Archiving SDK objects -> $@"
	@mkdir -p $(dir $@)
	@ar rcs $@ $(SDK_OBJS) $(SDK_C_OBJS) $(SDK_EXTRA_OBJS)

# Rule to build SDK sources (in-tree)
build/7zip/%.o: $(SDK_ROOT)/%.cpp
	@echo "CXX sdk $< -> $@"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I./src/7zip/CPP -I./src/7zip/CPP/7zip -I./src/generator -include src/generator/compat_prefix.h -c $< -o $@

# Rule to build extra local cpp sources into the SDK object tree
build/7zip/%.o: %.cpp
	@echo "CXX local $< -> $@"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I./src/7zip/CPP -I./src/7zip/CPP/7zip -c $< -o $@

# Rule to build C sources from src/7zip/C into the SDK object tree
build/7zip/C/%.o: $(SDK_ROOT)/../../C/%.c
	@echo "CC sdk C $< -> $@"
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -I./src/7zip/C -I./src/7zip/CPP -I./src/7zip/CPP/7zip -c $< -o $@

# specific rule for sources under src/generator
build/7zip/generator/%.o: src/generator/%.cpp
	@echo "CXX gen $< -> $@"
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -I./src/7zip/CPP -I./src/7zip/CPP/7zip -c $< -o $@

generator: $(SDK_PREREQS)
	@if [ "$(ENABLE_SDK_GENERATOR)" -ne "1" ]; then \
		mkdir -p bin; \
		echo "Building CLI-only generator (requires '7z' in PATH)"; \
		$(CXX) $(CXXFLAGS) -o bin/create_deflate64 src/generator/create_deflate64.cpp || (echo "generator build failed"; exit 1); \
		echo "built bin/create_deflate64"; \
	else \
		if [ -d src/7zip ]; then \
		mkdir -p bin; \
		echo "Building SDK-backed generator and linking with SDK library"; \
			$(CXX) $(CXXFLAGS) -DUSE_7ZIP_SDK -I./src/7zip/CPP -I./src/7zip/CPP/7zip -o bin/create_deflate64 src/generator/create_deflate64.cpp build/7zip/Compress/Deflate64Register.o build/lib7zip.a || (echo "generator build failed"; exit 1); \
		echo "built bin/create_deflate64"; \
		else \
			echo "skipping generator build: src/7zip not present (run 'make src/7zip' to fetch)"; \
		fi; \
	fi

# Helper: download and extract 7-Zip SDK into src/7zip
.PHONY: src/7zip
src/7zip:
	@echo "Downloading 7-Zip LZMA SDK and extracting into src/7zip";
	@mkdir -p tmp
	@test -f tmp/lzma2501.7z || curl -sSL -o tmp/lzma2501.7z https://7-zip.org/a/lzma2501.7z
	@mkdir -p src
	@rm -rf src/7zip
	@echo "Extracting tmp/lzma2501.7z -> src/7zip";
	@mkdir -p src/7zip
	@command -v 7z >/dev/null 2>&1 || (echo "7z not found: install p7zip to extract the SDK or extract tmp/lzma2501.7z manually"; exit 1)
	@7z x -y -otmp tmp/lzma2501.7z >/dev/null 2>&1 || (echo "failed to extract SDK with 7z"; exit 1)
	@# The archive contains a 7zip folder; move it to src/7zip/CPP
	@if [ -d tmp/CPP ]; then mv tmp/CPP src/7zip/CPP; elif [ -d tmp/7z ]; then mv tmp/7z src/7zip/CPP; else mv tmp/* src/7zip/CPP 2>/dev/null || true; fi
	@echo "SDK extracted to src/7zip/CPP"