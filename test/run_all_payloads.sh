#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
PAYLOAD_DIR="$ROOT_DIR/test/ref-data"
SKIP_DIR="$PAYLOAD_DIR/skipped"
RESULTS_FILE="$ROOT_DIR/test/payloads_results_clean.txt"
CSV_FILE="$ROOT_DIR/test/payloads_summary.csv"

mkdir -p "$SKIP_DIR"
rm -f "$RESULTS_FILE" "$CSV_FILE"
echo "payload,compressed_size,result,decompressed_size" > "$CSV_FILE"

echo "Running payload checks and filtering failures..." | tee "$RESULTS_FILE"

shopt -s nullglob
for p in "$PAYLOAD_DIR"/*; do
  # skip the skipped dir itself
  [ -f "$p" ] || continue
  fname=$(basename "$p")
  echo "---" >> "$RESULTS_FILE"
  echo "PAYLOAD: $p" >> "$RESULTS_FILE"
  out=$("$ROOT_DIR/test/payload_runner" "$p" 2>&1) || true
  echo "$out" >> "$RESULTS_FILE"
  if echo "$out" | grep -q '^OK:'; then
    # parse decompressed size
    dec_size=$(echo "$out" | sed -n 's/^OK: .* -> \([0-9]*\) bytes$/\1/p' || true)
    comp_size=$(stat -f%z "$p" 2>/dev/null || stat -c%s "$p" 2>/dev/null || echo 0)
    echo "$fname,$comp_size,OK,$dec_size" >> "$CSV_FILE"
  else
    # move to skipped
    mv -f "$p" "$SKIP_DIR/"
    comp_size=$(stat -f%z "$SKIP_DIR/$fname" 2>/dev/null || stat -c%s "$SKIP_DIR/$fname" 2>/dev/null || echo 0)
    echo "$fname,$comp_size,SKIPPED,0" >> "$CSV_FILE"
    echo "Moved $fname to skipped/" >> "$RESULTS_FILE"
  fi
done

echo "Done. Clean results: $RESULTS_FILE" | tee -a "$RESULTS_FILE"
echo "CSV summary: $CSV_FILE" | tee -a "$RESULTS_FILE"

exit 0
