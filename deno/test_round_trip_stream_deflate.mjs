import { fs, path, crypto, Buffer } from './compat.mjs';

if (Deno.args.length < 1) {
  console.error('usage: deno run --allow-read deno/test_round_trip_stream_deflate.mjs [wasm]');
  Deno.exit(2);
}
const wasmPath = Deno.args[0];
if (!fs.existsSync(wasmPath)) { console.error('wasm not found:', wasmPath); Deno.exit(2); }

(async () => {
  const wasmBuf = fs.readFileSync(wasmPath);
  const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: ()=>{} } });
  const exp = instance.exports;
  globalThis.WASM_EXPORTS = exp;

  // import the ESM wrapper
  const mod = await import('../src/wasm/api/compression-streams.js');
  const { CompressionStreamZlib, DecompressionStreamZlib } = mod;

  const LEN = 24000;
  const srcBuf = Buffer.allocUnsafe(LEN);
  crypto.randomFillSync(srcBuf);

  const pump = new TransformStream();
  const writer = pump.writable.getWriter();

  const cs = new CompressionStreamZlib('deflate', { wasm: exp });
  const ds = new DecompressionStreamZlib('deflate', { wasm: exp });
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
    Deno.exit(3);
  }
  if (Buffer.compare(srcBuf, outBuf) !== 0) {
    console.error('ROUNDTRIP FAILED: data mismatch');
    Deno.exit(4);
  }

  console.log('ROUNDTRIP OK');
  Deno.exit(0);
})();
