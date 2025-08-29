#!/usr/bin/env node
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

function usage() {
  console.error('usage: node run_roundtrip_cli.js [format] [size] [wasm]');
  console.error('  format: deflate | gzip | deflate-raw | deflate64-raw');
  console.error('  size: bytes (integer)');
  console.error('  wasm: path to dist/zlib_streams.wasm');
  process.exit(2);
}

const format = process.argv[2] || 'deflate';
const size = parseInt(process.argv[3] || '24000', 10);
const wasmPath = process.argv[4] || path.join('dist','zlib_streams.wasm');
if (!fs.existsSync(wasmPath)) { console.error('wasm not found:', wasmPath); usage(); }
if (!['deflate','gzip','deflate-raw','deflate64-raw'].includes(format)) { console.error('unknown format:', format); usage(); }
if (!Number.isFinite(size) || size <= 0) { console.error('invalid size:', size); usage(); }

(async ()=>{
  const startAll = Date.now();
  const wasmBuf = fs.readFileSync(wasmPath);
  const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: ()=>{} } });
  const exp = instance.exports;
  globalThis.WASM_EXPORTS = exp;

  const mod = await import('../api/compression-streams.js');
  const { CompressionStream, DecompressionStream } = mod;

  console.log('format=%s size=%d wasm=%s', format, size, wasmPath);

  const srcBuf = Buffer.allocUnsafe(size);
  crypto.randomFillSync(srcBuf);

  const pump = new TransformStream();
  const writer = pump.writable.getWriter();

  const cs = new CompressionStream(format, { wasm: exp });
  const ds = new DecompressionStream(format, { wasm: exp });
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

  const t0 = Date.now();
  await Promise.all([readerTask, writerTask]);
  const t1 = Date.now();

  const outBuf = Buffer.concat(outChunks);
  if (outBuf.length !== srcBuf.length) {
    console.error('ROUNDTRIP FAILED: length mismatch', srcBuf.length, outBuf.length);
    process.exit(3);
  }
  if (Buffer.compare(srcBuf, outBuf) !== 0) {
    console.error('ROUNDTRIP FAILED: data mismatch');
    process.exit(4);
  }

  const elapsed = (t1 - t0) / 1000;
  const throughput = (size / (1024*1024)) / elapsed;
  console.log('ROUNDTRIP OK in %dms (%d MB/s)', t1 - t0, throughput.toFixed(2));
  console.log('total runtime %dms', Date.now() - startAll);
  process.exit(0);
})();
