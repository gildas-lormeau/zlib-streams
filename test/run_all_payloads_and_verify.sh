#!/usr/bin/env bash
set -euo pipefail
OUTDIR=tmp/all_runs
rm -rf "$OUTDIR"
mkdir -p "$OUTDIR"
BINS=(test/payload_decompress_ref_debug test/payload_decompress_test_debug test/payload_decompress_nowindow_debug)
echo "Running harnesses on all payloads in test/ref-data -> $OUTDIR"
for p in test/ref-data/*.deflate64; do
  [ -f "$p" ] || continue
  pb=$(basename "$p")
  echo "Payload: $pb"
  for bin in "${BINS[@]}"; do
    bbase=$(basename "$bin")
    out="$OUTDIR/${bbase}__${pb}.out"
    log="$OUTDIR/${bbase}__${pb}.log"
    if [ ! -x "$bin" ]; then
      echo "  missing binary: $bin"
      printf "missing binary: %s\n" "$bin" >"$log"
      continue
    fi
    # run harness and capture RC
    "$bin" "$p" "$out" >"$log" 2>&1 || true
    rc=$?
    echo "  ran: $bbase -> rc=$rc"
  done
done

# compute shas and compare
python3 - <<'PY'
import hashlib,glob,os
from collections import defaultdict
outdir='tmp/all_runs'
files=sorted(glob.glob(os.path.join(outdir,'*__*.out')))
if not files:
    print('No output files found in', outdir)
    raise SystemExit(1)

groups=defaultdict(list)
for f in files:
    base=os.path.basename(f)
    binname,payload=base.split('__',1)
    if payload.endswith('.out'):
        payload=payload[:-4]
    with open(f,'rb') as fh:
        h=hashlib.sha256(fh.read()).hexdigest()
    groups[payload].append((binname,h))

print('\nVerification results:')
for payload in sorted(groups):
    print('Payload:', payload)
    for binname,h in groups[payload]:
        print(' ', binname, h)
    hashes=set(h for _,h in groups[payload])
    print('  Result:', 'OK' if len(hashes)==1 else 'MISMATCH')
    print()
PY
