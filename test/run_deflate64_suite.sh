#!/usr/bin/env bash
set -euo pipefail
OUTDIR=tmp/all_runs
rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"
# Collect deflate64-related payloads from test/ref-data and deduplicate by basename.
# Match both '*def64*' and '*deflate64*' and ensure each basename is used only once.
PAYLOADS_RAW=(test/ref-data/*def64* test/ref-data/*deflate64*)
PAYLOADS=()

# Convert the raw paths to basenames, sort-unique, then re-expand to full paths.
basenames=$(printf '%s\n' "${PAYLOADS_RAW[@]}" | xargs -n1 basename 2>/dev/null | sort -u)
for b in $basenames; do
    p="test/ref-data/$b"
    [ -f "$p" ] || continue
    PAYLOADS+=("$p")
done

BINS=(test/payload_decompress_ref_debug test/payload_decompress_test_debug test/payload_decompress_nowindow_debug)

for p in "${PAYLOADS[@]}"; do
  [ -f "$p" ] || continue
  pb=$(basename "$p")
  echo "Payload: $pb"
  for bin in "${BINS[@]}"; do
    bbase=$(basename "$bin")
    out="$OUTDIR/${bbase}__${pb}.out"
    log="$OUTDIR/${bbase}__${pb}.log"
    "$bin" "$p" "$out" >"$log" 2>&1 || true
    rc=$?
    echo "  ran: $bbase -> rc=$rc"
  done
done

# Prepare an explicit payload list for the wasm runner (pass basenames)
WASM_ARGS=()
for p in "${PAYLOADS[@]}"; do
    [ -f "$p" ] || continue
    WASM_ARGS+=("$(basename "$p")")
done

# Run the wasm streaming runner with the explicit Deflate64 payload list
node test/run_wasm_all.js "${WASM_ARGS[@]}" || true

# Compute and show sha256s
python3 - <<'PY'
import hashlib,glob,os
outdir='tmp/all_runs'
files=sorted(glob.glob(os.path.join(outdir,'*__*.out')))
if not files:
    print('No output files found in', outdir)
    raise SystemExit(1)
groups={}
for f in files:
    base=os.path.basename(f)
    binname,payload=base.split('__',1)
    with open(f,'rb') as fh:
        h=hashlib.sha256(fh.read()).hexdigest()
    groups.setdefault(payload,[]).append((binname,h))

print('\nVerification results:')
for payload in sorted(groups):
    print('Payload:', payload)
    for binname,h in groups[payload]:
        print(' ', binname, h)
    hashes=set(h for _,h in groups[payload])
    print('  Result:', 'OK' if len(hashes)==1 else 'MISMATCH')
    print()
PY
