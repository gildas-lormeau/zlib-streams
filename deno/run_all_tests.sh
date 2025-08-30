#!/usr/bin/env bash
set -euo pipefail

WASM=${1:-dist/zlib-streams.wasm}
DENO=${DENO:-deno}
# Don't include --compat (not available in all Deno builds). Use granular unstable flags + permissions.
# `--unstable-detect-cjs` lets Deno detect CommonJS modules (so older Node-style tests may run under Deno)
DENOFLAGS="--unstable-detect-cjs --allow-read --allow-run --allow-write --allow-env"

echo "Running Deno test suite against wasm: $WASM"

failures=0

echo "\n== Running Deno-native tests (deno/*.mjs) =="
for f in deno/*.mjs; do
  echo "\n-- $f --"
  if ! $DENO run $DENOFLAGS -- $f $WASM; then
    echo "FAILED: $f"
    failures=$((failures+1))
  fi
done

echo "\n== Attempting Node-style tests under Deno compat (src/wasm/tests/*.js) =="
for f in src/wasm/tests/*.js; do
  echo "\n-- $f --"
  # prepare default roundtrip input used by several tests
  mkdir -p tmp/all_runs
  if [ ! -f tmp/all_runs/roundtrip_input3.bin ]; then
    echo "Creating tmp/all_runs/roundtrip_input3.bin (24000 bytes, deflateRaw)"
    if command -v node >/dev/null 2>&1; then
      node -e "const fs=require('fs'), z=require('zlib'); fs.writeFileSync('tmp/all_runs/roundtrip_input3.bin', z.deflateRawSync(Buffer.alloc(24000, 'x')));"
    else
      # fallback: create a raw filler file
      dd if=/dev/zero bs=24000 count=1 of=tmp/all_runs/roundtrip_input3.bin 2>/dev/null || head -c 24000 /dev/zero > tmp/all_runs/roundtrip_input3.bin
    fi
  fi

  # Try running the Node-style test under Deno (may fail due to CommonJS require)
  # select per-test default args when necessary
  basef=$(basename "$f")
  case "$basef" in
    run_roundtrip_cli.js)
      extra_args=(deflate 24000 "$WASM") ;;
    test_inflate9_stream.js)
      if [ -f test/ref-data/10k_lines.deflate64 ]; then
        extra_args=("$WASM" test/ref-data/10k_lines.deflate64 tmp/all_runs/wasm__10k_lines.deflate64.out);
      else
        extra_args=("$WASM" tmp/all_runs/roundtrip_input3.bin tmp/all_runs/wasm__roundtrip_input.out);
      fi ;;
    test_inflate_stream.js)
      # pick a small ref payload if present, else use the generated roundtrip_input3.bin
      if [ -f test/ref-data/10k_lines.deflate64 ]; then
        extra_args=("$WASM" test/ref-data/10k_lines.deflate64 tmp/all_runs/inflate_stream_out.bin) ;
      else
        extra_args=("$WASM" tmp/all_runs/roundtrip_input3.bin tmp/all_runs/inflate_stream_out.bin) ;
      fi ;;
    test_round_trip_stream_mem.js)
      extra_args=("$WASM" tmp/all_runs/roundtrip_input3.bin) ;;
    test_round_trip_stream.js)
      extra_args=("$WASM" tmp/all_runs/roundtrip_input3.bin tmp/all_runs/roundtrip_out__sample.bin) ;;
    *)
      extra_args=("$WASM") ;;
  esac

  if $DENO run $DENOFLAGS -- $f "${extra_args[@]}"; then
    echo "OK (deno): $f"
    continue
  fi
  # If Deno fails, fallback to Node if available
  if command -v node >/dev/null 2>&1; then
    echo "Falling back to Node for $f with args: ${extra_args[*]}"
    if node $f ${extra_args[@]}; then
      echo "OK (node): $f"
    else
      echo "FAILED (node): $f"
      failures=$((failures+1))
    fi
  else
    echo "SKIPPED: no Node available and Deno failed: $f"
    failures=$((failures+1))
  fi
done

echo "\nFinished. failures=${failures}"
exit ${failures}
