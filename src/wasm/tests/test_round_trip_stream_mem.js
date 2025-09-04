import { readFileSync } from 'fs';
import path from 'path';
if (process.argv.length < 4) {
  console.error('usage: node test_round_trip_stream_mem.js <wasm> <infile>');
  process.exit(2);
}
const wasmPath = process.argv[2];
const inPath = process.argv[3];

const buf = readFileSync(inPath);
const wasmBuf = readFileSync(wasmPath);

(async () => {
  const { instance } = await WebAssembly.instantiate(wasmBuf, {
    env: { emscripten_notify_memory_growth: () => {} }
  });
  const exp = instance.exports;
  const memory = exp.memory;
  const HEAP = new Uint8Array(memory.buffer);

  // deflate setup
  const dptr = exp.deflate_new();
  if (dptr === 0) throw new Error('deflate alloc failed');
  let r = exp.deflate_init(dptr, -1);
  if (r !== 0) throw new Error('deflate init failed: '+r);

  const inPtr = exp.malloc(buf.length);
  if (!inPtr) throw new Error('malloc failed for input');
  HEAP.set(buf, inPtr);

  const OUT_WINDOW = 65536;
  const outPtr = exp.malloc(OUT_WINDOW);
  if (!outPtr) throw new Error('malloc failed for output');

  // inflate setup (we stream into it)
  const iptrOut = exp.malloc(OUT_WINDOW);
  if (!iptrOut) throw new Error('malloc failed for inflate output');
  const zptr = exp.inflate_new();
  if (zptr === 0) throw new Error('inflate alloc failed');
  r = exp.inflate_init(zptr);
  if (r !== 0) throw new Error('inflate init failed: '+r);

  const compHeapPtr = exp.malloc(OUT_WINDOW);
  if (!compHeapPtr) throw new Error('malloc failed for comp shuttle');

  let compOutPos = 0;
  let decompOutPos = 0;
  let availIn = buf.length;
  let off = 0;
  let deflate_done = false;
  let inflate_done = false;

  const decompChunks = [];

  // single streaming loop
  while (!(deflate_done && inflate_done)) {
    if (!deflate_done) {
      const toRead = Math.min(availIn, 32768);
      const flush = (availIn === 0) ? 4 : 0;
      const ret = exp.deflate_process(dptr, inPtr + off, toRead, outPtr + compOutPos, OUT_WINDOW - compOutPos, flush);
      const produced = ret & 0x00ffffff;
      let code = (ret >> 24) & 0xff;
      if (code & 0x80) code = code - 0x100;

      if (produced > 0) {
        const slice = HEAP.subarray(outPtr + compOutPos, outPtr + compOutPos + produced);
        HEAP.set(slice, compHeapPtr);
        // feed into inflater
        let compOff = 0;
        let compAvail = produced;
        while (compAvail > 0 && !inflate_done) {
          const infToRead = compAvail;
          const infFlush = (code === 1 && compAvail === infToRead && availIn === 0) ? 4 : 0;
          const r2 = exp.inflate_process(zptr, compHeapPtr + compOff, infToRead, iptrOut + decompOutPos, OUT_WINDOW - decompOutPos, infFlush);
          if (r2 < 0) throw new Error('inflate process error: '+r2);
          const produced2 = r2 & 0x00ffffff;
          const code2 = (r2 >> 24) & 0xff;
          if (produced2 > 0) {
            decompChunks.push(Buffer.from(HEAP.subarray(iptrOut + decompOutPos, iptrOut + decompOutPos + produced2)));
          }
          const consumed2 = exp.inflate_last_consumed(zptr);
          compOff += consumed2;
          compAvail -= consumed2;
          if (produced2 === (OUT_WINDOW - decompOutPos)) {
            decompOutPos = 0;
          } else {
            decompOutPos += produced2;
          }
          if (code2 === 1) {
            inflate_done = true;
            break;
          }
          if (consumed2 === 0) break;
        }
      }

      const consumed = exp.deflate_last_consumed(dptr);
      off += consumed;
      availIn -= consumed;

      if (produced === (OUT_WINDOW - compOutPos)) {
        compOutPos = 0;
      } else {
        compOutPos += produced;
      }

      if (code === 1) deflate_done = true;
    }

    if (deflate_done && !inflate_done) {
      const r2 = exp.inflate_process(zptr, compHeapPtr, 0, iptrOut + decompOutPos, OUT_WINDOW - decompOutPos, 4);
      if (r2 < 0) throw new Error('inflate process error while draining: '+r2);
      const produced2 = r2 & 0x00ffffff;
      const code2 = (r2 >> 24) & 0xff;
      if (produced2 > 0) decompChunks.push(Buffer.from(HEAP.subarray(iptrOut + decompOutPos, iptrOut + decompOutPos + produced2)));
      const consumed2 = exp.inflate_last_consumed(zptr);
      if (produced2 === (OUT_WINDOW - decompOutPos)) {
        decompOutPos = 0;
      } else {
        decompOutPos += produced2;
      }
      if (code2 === 1) inflate_done = true;
      if (produced2 === 0 && consumed2 === 0) break;
    }
  }

  exp.deflate_end(dptr);
  exp.inflate_end(zptr);

  const decompBuf = Buffer.concat(decompChunks);
  if (Buffer.compare(buf, decompBuf) === 0) {
    console.log('ROUNDTRIP OK');
    process.exit(0);
  } else {
    console.error('ROUNDTRIP FAILED: output differs');
    process.exit(3);
  }
})();
