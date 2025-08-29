const fs = require('fs');
const path = require('path');
if (process.argv.length < 5) {
  console.error('usage: node test_inflate_stream.js <wasm> <infile> <outfile>');
  process.exit(2);
}
const wasmPath = process.argv[2];
const inPath = process.argv[3];
const outPath = process.argv[4];

const buf = fs.readFileSync(inPath);
const wasmBuf = fs.readFileSync(wasmPath);

(async () => {
  const { instance } = await WebAssembly.instantiate(wasmBuf, {
    env: {
      emscripten_notify_memory_growth: () => {}
    }
  });
  const exp = instance.exports;
  const memory = exp.memory;
  const HEAP = new Uint8Array(memory.buffer);

  const zptr = exp.inflate9_new();
  if (zptr === 0) throw new Error('alloc failed');
  // payloads are raw deflate64 (no zlib/gzip header)
  let r = exp.inflate9_init(zptr);
  if (r !== 0) throw new Error('init failed: '+r);

  // allocate buffers in wasm heap (avoid overwriting static data at low addresses)
  const inPtr = exp.malloc(buf.length);
  if (!inPtr) throw new Error('malloc failed for input');
  HEAP.set(buf, inPtr);
  const OUT_WINDOW = 65536;
  const outPtr = exp.malloc(OUT_WINDOW);
  if (!outPtr) throw new Error('malloc failed for output');
  let outPos = 0;
  let availIn = buf.length;
  let off = 0;
  while (true) {
    const toRead = Math.min(availIn, 32768);
  // use Z_FINISH in the main loop when we've supplied the last input
  // to mirror the native harness and avoid duplicate final calls.
  const flush = (availIn === 0) ? 4 : 0; /* Z_FINISH when no more input, else Z_NO_FLUSH */
  const ret = exp.inflate9_process(zptr, inPtr+off, toRead, outPtr + outPos, OUT_WINDOW - outPos, flush);
  // decode packed return: low 24 bits = produced, high 8 bits = zlib code
  const produced = ret & 0x00ffffff;
  let code = (ret >> 24) & 0xff;
  // convert to signed 8-bit if it represents a negative zlib error (e.g., 0xfb == -5)
  if (code & 0x80) code = code - 0x100;
    // copy produced out
  const outBuf = Buffer.from(HEAP.subarray(outPtr + outPos, outPtr + outPos + produced));
  fs.appendFileSync(outPath, outBuf);
  /* Advance by the number of input bytes actually consumed by the
   * wasm inflate9 implementation. This prevents truncating the compressed
   * stream when inflate9 does not consume the whole supplied chunk.
   */
  const consumed = exp.inflate9_last_consumed(zptr);
  off += consumed;
  availIn -= consumed;
      if (produced === (OUT_WINDOW - outPos)) {
        outPos = 0;
      } else {
        outPos += produced;
      }
    if (code === 1) break; // Z_STREAM_END
  }
  // No separate finalize loop: the main loop used Z_FINISH when no input
  // remained and continues until the stream reports Z_STREAM_END.
  exp.inflate9_end(zptr);
  console.log('done');
})();
