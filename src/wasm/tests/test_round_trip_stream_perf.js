const fs = require('fs');
const path = require('path');
const crypto = require('crypto');
const { performance } = require('perf_hooks');

if (process.argv.length < 2) {
  console.error('usage: node test_round_trip_stream_perf.js [wasm]');
  process.exit(2);
}
const wasmPath = process.argv[2] || path.join('dist','zlib-streams-dev.wasm');
if (!fs.existsSync(wasmPath)) { console.error('wasm not found:', wasmPath); process.exit(2); }

(async ()=>{
  const wasmBuf = fs.readFileSync(wasmPath);
  const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: ()=>{} } });
  const exp = instance.exports;
  globalThis.WASM_EXPORTS = exp;

  const mod = await import('../api/zlib-streams.js');
  const { CompressionStreamZlib, DecompressionStreamZlib } = mod;

  // Parse optional CLI flags:
  // --packets=65536,32768 (bytes)
  // --include-large  (include 500MB and 1024MB in the test sizes)
  const argv = process.argv.slice(2);
  let packetArg = argv.find(a => a.startsWith('--packets='));
  let includeLarge = argv.includes('--include-large');
  // output mode: --output=zero (default) or --output=buffer
  let outputArg = argv.find(a => a.startsWith('--output='));
  const OUTPUT_MODE = outputArg ? outputArg.split('=')[1] : 'zero';
  // default: single 64KB packet size
  const PACKETS = packetArg ? packetArg.split('=')[1].split(',').map(s => Number(s)) : [64 * 1024];
  // median runs: --median=3 (default)
  let medianArg = argv.find(a => a.startsWith('--median='));
  const MEDIAN_RUNS = medianArg ? Number(medianArg.split('=')[1]) : 3;
  const measureRss = argv.includes('--measure-rss');
  // default sizes: 10MB, 50MB, 100MB. Large sizes optional via --include-large
  const sizes = [10 * 1024 * 1024, 50 * 1024 * 1024, 100 * 1024 * 1024];
  if (includeLarge) {
    sizes.push(500 * 1024 * 1024, 1024 * 1024 * 1024);
  }

  function hrSec(start) { return Number(process.hrtime.bigint() - start) / 1e9; }

  for (const size of sizes) {
    console.log('\n=== SIZE %s MB ===', (size / (1024*1024)).toFixed(0));
    for (const PACKET of PACKETS) {
      console.log('--- packet size %d bytes ---', PACKET);
      try {
        // collect per-run metrics then report medians
        const compRuns = [];
        const decompRuns = [];
        const roundRuns = [];
        const rssRuns = [];

        // reuse the top-level wasm exports for all runs to avoid repeated instantiate overhead
        const expRun = exp;
        globalThis.WASM_EXPORTS = expRun;

        // per-run stream options (zero-copy by default, allow forcing Buffer output)
        const streamOpts = (() => {
          if (OUTPUT_MODE === 'buffer') return { wasm: expRun, forceBuffer: true, zeroCopyOutput: false };
          // 'zero' or any other value -> zero-copy output
          return { wasm: expRun, zeroCopyOutput: true };
        })();

        // allocate and fill source buffer once per size and reuse for MEDIAN runs
        const src = Buffer.allocUnsafe(size);
        crypto.randomFillSync(src);

        for (let run = 0; run < MEDIAN_RUNS; run++) {
          // --- compress-only ---
          const pumpC = new TransformStream();
          const writerC = pumpC.writable.getWriter();
          const cs = new CompressionStreamZlib('deflate-raw', streamOpts);
          const compStream = pumpC.readable.pipeThrough(cs);
          const compReader = compStream.getReader();

          const compChunks = [];
          const compTask = (async () => {
            while (true) {
              const { done, value } = await compReader.read();
              if (done) break;
              compChunks.push(Buffer.from(value));
            }
          })();

          const t0c = process.hrtime.bigint();
          for (let off = 0; off < src.length; off += PACKET) {
            const slice = src.subarray(off, Math.min(off + PACKET, src.length));
            await writerC.write(slice);
          }
          await writerC.close();
          await compTask;
          const t1c = process.hrtime.bigint();

          const compBuf = Buffer.concat(compChunks);
          const compSec = Number(t1c - t0c) / 1e9;
          const compMBs = (size / (1024 * 1024)) / compSec;
          compRuns.push(compMBs);

          // --- decompress-only ---
          const pumpD = new TransformStream();
          const writerD = pumpD.writable.getWriter();
          const ds = new DecompressionStreamZlib('deflate-raw', streamOpts);
          const decompStream = pumpD.readable.pipeThrough(ds);
          const decompReader = decompStream.getReader();

          const decompChunks = [];
          const decompTask = (async () => {
            while (true) {
              const { done, value } = await decompReader.read();
              if (done) break;
              decompChunks.push(Buffer.from(value));
            }
          })();

          const t0d = process.hrtime.bigint();
          for (let off = 0; off < compBuf.length; off += PACKET) {
            const slice = compBuf.subarray(off, Math.min(off + PACKET, compBuf.length));
            await writerD.write(slice);
          }
          await writerD.close();
          await decompTask;
          const t1d = process.hrtime.bigint();

          const decompBuf = Buffer.concat(decompChunks);
          const decompSec = Number(t1d - t0d) / 1e9;
          const decompMBs = (size / (1024 * 1024)) / decompSec;
          decompRuns.push(decompMBs);

          // validate
          if (decompBuf.length !== src.length) {
            console.error('DECOMPRESSED LENGTH MISMATCH', decompBuf.length, src.length);
            process.exit(3);
          }
          if (Buffer.compare(src, decompBuf) !== 0) {
            console.error('DECOMPRESSED CONTENT MISMATCH');
            process.exit(4);
          }

          // --- combined roundtrip (write src -> cs -> ds -> read) ---
          const pumpR = new TransformStream();
          const writerR = pumpR.writable.getWriter();
          const csR = new CompressionStreamZlib('deflate-raw', streamOpts);
          const dsR = new DecompressionStreamZlib('deflate-raw', streamOpts);
          const outStream = pumpR.readable.pipeThrough(csR).pipeThrough(dsR);
          const readerR = outStream.getReader();

          const outChunks = [];
          const readerTask = (async () => {
            while (true) {
              const { done, value } = await readerR.read();
              if (done) break;
              outChunks.push(Buffer.from(value));
            }
          })();

          const t0r = process.hrtime.bigint();
          for (let off = 0; off < src.length; off += PACKET) {
            const slice = src.subarray(off, Math.min(off + PACKET, src.length));
            await writerR.write(slice);
          }
          await writerR.close();
          await readerTask;
          const t1r = process.hrtime.bigint();

          const outBuf = Buffer.concat(outChunks);
          const roundSec = Number(t1r - t0r) / 1e9;
          const roundMBs = (size / (1024 * 1024)) / roundSec;
          roundRuns.push(roundMBs);

          if (measureRss) rssRuns.push(process.memoryUsage().rss);
        }

        // helper: median
        function median(arr) {
          const a = arr.slice().sort((x, y) => x - y);
          const m = Math.floor(a.length / 2);
          return (a.length % 2) ? a[m] : (a[m - 1] + a[m]) / 2;
        }

        console.log('compress-only median MB/s:', median(compRuns).toFixed(2), 'runs=', compRuns.map(r => r.toFixed(2)).join(', '));
        console.log('decompress-only median MB/s:', median(decompRuns).toFixed(2), 'runs=', decompRuns.map(r => r.toFixed(2)).join(', '));
        console.log('roundtrip median MB/s:', median(roundRuns).toFixed(2), 'runs=', roundRuns.map(r => r.toFixed(2)).join(', '));
        if (measureRss) {
          console.log('median RSS (bytes):', median(rssRuns), 'runs=', rssRuns.join(', '));
        }

      } catch (e) {
        console.error('ERROR for size', size, e && e.stack || e);
      }
    }
  }

  console.log('\nPerf tests completed');
  process.exit(0);
})();
