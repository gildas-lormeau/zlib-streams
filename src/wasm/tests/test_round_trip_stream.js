const fs = require('fs');
const path = require('path');
if (process.argv.length < 5) {
  console.error('usage: node test_round_trip_stream.js <wasm> <infile> <out_decompressed>');
  process.exit(2);
}
const wasmPath = process.argv[2];
const inPath = process.argv[3];
const outPath = process.argv[4];

const buf = fs.readFileSync(inPath);
const wasmBuf = fs.readFileSync(wasmPath);

function packRet(ret) {
  return { produced: ret & 0x00ffffff, code: (ret >> 24) & 0xff };
}

(async () => {
  const { instance } = await WebAssembly.instantiate(wasmBuf, {
    env: { emscripten_notify_memory_growth: () => {} }
  });
  const exp = instance.exports;
  const memory = exp.memory;
  const HEAP = new Uint8Array(memory.buffer);

  // --- Deflate (compress) ---
  const dptr = exp.wasm_deflate_new();
  if (dptr === 0) throw new Error('deflate alloc failed');
  let r = exp.wasm_deflate_init(dptr); // zlib header by default
  if (r !== 0) throw new Error('deflate init failed: '+r);

  const inPtr = exp.malloc(buf.length);
  if (!inPtr) throw new Error('malloc failed for input');
  HEAP.set(buf, inPtr);

  const OUT_WINDOW = 65536;
  const outPtr = exp.malloc(OUT_WINDOW);
  if (!outPtr) throw new Error('malloc failed for output');

  let compOutPos = 0;
  let availIn = buf.length;
  let off = 0;
  const compChunks = [];

  // Allocate a small heap buffer to shuttle produced compressed bytes into
  const compHeapPtr = exp.malloc(OUT_WINDOW);
  if (!compHeapPtr) throw new Error('malloc failed for comp shuttle');

  // Prepare inflater now so we can stream compressed data to it immediately
  const iptrOut = exp.malloc(OUT_WINDOW);
  if (!iptrOut) throw new Error('malloc failed for inflate output');
  const zptr = exp.wasm_inflate_new();
  if (zptr === 0) throw new Error('inflate alloc failed');
  r = exp.wasm_inflate_init(zptr); // expects zlib header
  if (r !== 0) throw new Error('inflate init failed: '+r);

  let deflate_done = false;
  let inflate_done = false;

  // clear output file before streaming
  fs.writeFileSync(outPath, '');

  // single streaming loop: drive deflate then feed produced bytes into inflate
  let decompOutPos = 0;
  while (!(deflate_done && inflate_done)) {
    // --- step deflate (if not finished) ---
    if (!deflate_done) {
      const toRead = Math.min(availIn, 32768);
      const flush = (availIn === 0) ? 4 : 0; // Z_FINISH when no more input
  const ret = exp.wasm_deflate_process(dptr, inPtr + off, toRead, outPtr + compOutPos, OUT_WINDOW - compOutPos, flush);
      const produced = ret & 0x00ffffff;
      let code = (ret >> 24) & 0xff;
      if (code & 0x80) code = code - 0x100; // signed conversion

      if (produced > 0) {
        const slice = HEAP.subarray(outPtr + compOutPos, outPtr + compOutPos + produced);
        // record compressed bytes
        compChunks.push(Buffer.from(slice));
        // copy into shuttle buffer for immediate inflate feeding
        HEAP.set(slice, compHeapPtr);
        // feed into inflater in a tight loop until consumed
        let compOff = 0;
        let compAvail = produced;
        while (compAvail > 0 && !inflate_done) {
          const infToRead = compAvail; // small chunks are fine
          const infFlush = (code === 1 && compAvail === infToRead && availIn === 0) ? 4 : 0;
          const r2 = exp.wasm_inflate_process(zptr, compHeapPtr + compOff, infToRead, iptrOut + decompOutPos, OUT_WINDOW - decompOutPos, infFlush);
          if (r2 < 0) throw new Error('inflate process error: '+r2);
          const produced2 = r2 & 0x00ffffff;
          const code2 = (r2 >> 24) & 0xff;
          if (produced2 > 0) {
            const outBuf = Buffer.from(HEAP.subarray(iptrOut + decompOutPos, iptrOut + decompOutPos + produced2));
            // append decompressed output immediately to disk buffer
            fs.appendFileSync(outPath, outBuf);
          }
          const consumed2 = exp.wasm_inflate_last_consumed(zptr);
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
          // if inflater didn't consume any input, break to avoid spin
          if (consumed2 === 0) break;
        }
      }

      const consumed = exp.wasm_deflate_last_consumed(dptr);
      off += consumed;
      availIn -= consumed;

      if (produced === (OUT_WINDOW - compOutPos)) {
        compOutPos = 0;
      } else {
        compOutPos += produced;
      }

      if (code === 1) {
        deflate_done = true;
      }
    }

    // --- if deflate is done but inflater not yet finished, drain the inflater ---
    if (deflate_done && !inflate_done) {
      // call inflate with zero new input but with Z_FINISH to flush remaining data
      const r2 = exp.wasm_inflate_process(zptr, compHeapPtr, 0, iptrOut + decompOutPos, OUT_WINDOW - decompOutPos, 4);
      if (r2 < 0) throw new Error('inflate process error while draining: '+r2);
      const produced2 = r2 & 0x00ffffff;
      const code2 = (r2 >> 24) & 0xff;
      if (produced2 > 0) {
        const outBuf = Buffer.from(HEAP.subarray(iptrOut + decompOutPos, iptrOut + decompOutPos + produced2));
        fs.appendFileSync(outPath, outBuf);
      }
      const consumed2 = exp.wasm_inflate_last_consumed(zptr);
      if (produced2 === (OUT_WINDOW - decompOutPos)) {
        decompOutPos = 0;
      } else {
        decompOutPos += produced2;
      }
      if (code2 === 1) {
        inflate_done = true;
      }
      // if no progress, break to avoid infinite loop
      if (produced2 === 0 && consumed2 === 0) break;
    }
  }

  // finalize
  exp.wasm_deflate_end(dptr);
  exp.wasm_inflate_end(zptr);

  const compBuf = Buffer.concat(compChunks);
  // ensure output dir
  fs.mkdirSync(path.dirname(outPath), { recursive: true });
  const base = path.basename(inPath);
  const compPath = path.join('tmp', 'all_runs', 'wasm_roundtrip_comp__' + base + '.out');
  fs.mkdirSync(path.dirname(compPath), { recursive: true });
  fs.writeFileSync(compPath, compBuf);

  // Decompressed output was streamed to `outPath` during the single-loop above.
  const decompBuf = fs.readFileSync(outPath);
  console.log('wrote', outPath);

  // Verify round-trip equality
  if (Buffer.compare(buf, decompBuf) === 0) {
    console.log('ROUNDTRIP OK');
    process.exit(0);
  } else {
    console.error('ROUNDTRIP FAILED: output differs');
    process.exit(3);
  }
})();
