import { existsSync, readFileSync, mkdirSync, writeFileSync } from 'fs';
import { join } from 'path';
import { randomFillSync } from 'crypto';

// Usage: node diagnose_stream_mem.js [wasm] [--iterations=N] [--packet=bytes] [--log-interval=10]
const wasmPath = process.argv[2] || join('dist','zlib-streams-dev.wasm');
let ITER = 700;
let PACKET = 1024;
let LOG_INTERVAL = 20;
for (const a of process.argv.slice(2)) {
  if (a.startsWith('--iterations=')) ITER = Number(a.split('=')[1]);
  if (a.startsWith('--packet=')) PACKET = Number(a.split('=')[1]);
  if (a.startsWith('--log-interval=')) LOG_INTERVAL = Number(a.split('=')[1]);
}
if (!existsSync(wasmPath)) { console.error('wasm not found:', wasmPath); process.exit(2); }

(async ()=>{
  const wasmBuf = readFileSync(wasmPath);
  const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: ()=>{} } });
  const exp = instance.exports;

  const mod = await import('../api/zlib-streams.js');
    const { CompressionStreamZlib, DecompressionStreamZlib, setWasmExports } = mod;
  setWasmExports(exp);

  function statsLine(iter) {
    return {
      iter,
      wasm_mem_bytes: exp.memory.buffer.byteLength,
      rss: process.memoryUsage().rss,
      arrayBuffers: process.memoryUsage().arrayBuffers,
    };
  }

  console.log('diagnose_stream_mem: iterations=%d packet=%d wasm=%s', ITER, PACKET, wasmPath);

  const lines = [];
  // small source buffer reused
  const src = Buffer.allocUnsafe(PACKET);
  randomFillSync(src);

  for (let i = 1; i <= ITER; i++) {
    // create a single pump and pipeline: write the src once
    const pump = new TransformStream();
    const writer = pump.writable.getWriter();
    const cs = new CompressionStreamZlib('deflate-raw');
    const ds = new DecompressionStreamZlib('deflate-raw');
    const reader = pump.readable.pipeThrough(cs).pipeThrough(ds).getReader();

    const readTask = (async ()=>{
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        // drop the data
      }
    })();

    // write one packet and close
    await writer.write(src);
    await writer.close();
    await readTask;

    // capture stats
  // optionally run GC if available to clear Node-managed ArrayBuffers
  try { if (typeof global.gc === 'function') global.gc(); } catch (e) {}
  const s = statsLine(i);
    lines.push(s);

    if (i % LOG_INTERVAL === 0 || i === 1 || i === ITER) {
      console.log('iter=%d wasm_mem=%dMB rss=%dMB arrayBuf=%d wasm_stats=%j', i,
        Math.round(s.wasm_mem_bytes / (1024*1024)), Math.round(s.rss / (1024*1024)), s.arrayBuffers, s.wasm_stats);
    }
  }

  console.log('\nfinal sample: ', lines[lines.length-1]);
  // write CSV for offline inspection
  try { mkdirSync(join('tmp','diagnose'), { recursive: true }); writeFileSync(join('tmp','diagnose','diagnose_stream_mem.json'), JSON.stringify(lines, null, 2)); } catch(e) {}
  console.log('wrote tmp/diagnose/diagnose_stream_mem.json');
  process.exit(0);
})();
