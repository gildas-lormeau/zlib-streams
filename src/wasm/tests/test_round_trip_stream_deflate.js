const fs = require('fs');
const path = require('path');

if (process.argv.length < 2) {
  console.error('usage: node test_round_trip_stream_deflate.js [wasm]');
  process.exit(2);
}
const wasmPath = process.argv[2] || path.join('dist','zlib_streams.wasm');
if (!fs.existsSync(wasmPath)) { console.error('wasm not found:', wasmPath); process.exit(2); }

(async ()=>{
  const wasmBuf = fs.readFileSync(wasmPath);
  const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: ()=>{} } });
  const exp = instance.exports;
  globalThis.WASM_EXPORTS = exp;

  const mod = await import('../api/compression-streams.js');
  const { CompressionStream, DecompressionStream } = mod;

  const LEN = 24000;
  const srcBuf = Buffer.allocUnsafe(LEN);
  require('crypto').randomFillSync(srcBuf);

  const pump = new TransformStream();
  const writer = pump.writable.getWriter();

  const cs = new CompressionStream('deflate', { wasm: exp });
  const ds = new DecompressionStream('deflate', { wasm: exp });
  const outStream = pump.readable.pipeThrough(cs).pipeThrough(ds);
  const reader = outStream.getReader();

  const outChunks = [];
  const readerTask = (async () => {
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      outChunks.push(Buffer.from(value));
    }
  })();

  const writerTask = (async () => {
    const CHUNK = 4096;
    for (let off = 0; off < srcBuf.length; off += CHUNK) {
      const slice = srcBuf.subarray(off, Math.min(off + CHUNK, srcBuf.length));
      await writer.write(slice);
    }
    await writer.close();
  })();

  await Promise.all([readerTask, writerTask]);

  const outBuf = Buffer.concat(outChunks);
  if (outBuf.length !== srcBuf.length) {
    console.error('ROUNDTRIP FAILED: length mismatch', srcBuf.length, outBuf.length);
    process.exit(3);
  }
  if (Buffer.compare(srcBuf, outBuf) !== 0) {
    console.error('ROUNDTRIP FAILED: data mismatch');
    process.exit(4);
  }

  console.log('ROUNDTRIP OK');
  process.exit(0);
})();
