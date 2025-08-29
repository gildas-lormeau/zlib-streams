const fs = require('fs');
const path = require('path');
const child = require('child_process');

if (process.argv.length < 3) {
  console.error('usage: node test_decompressionstream_inflate9.js <wasm>');
  process.exit(2);
}
const wasmPath = process.argv[2];
const refDir = path.join('test','ref-data');
const outDir = path.join('tmp','all_runs');
fs.mkdirSync(outDir, { recursive: true });

const files = fs.readdirSync(refDir).filter(f => f.indexOf('deflate64') !== -1);
if (files.length === 0) {
  console.error('no deflate64 payloads found in', refDir);
  process.exit(2);
}

(async ()=>{
  const wasmBuf = fs.readFileSync(wasmPath);
  const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: ()=>{} } });
  const exp = instance.exports;
  // prefer an explicit module-level setter so callers don't need to pass the wasm object
  const mod = await import('../api/compression-streams.js');
  if (typeof mod.setWasmExports === 'function') mod.setWasmExports(exp); else globalThis.WASM_EXPORTS = exp;
  const { DecompressionStream } = mod;

  let overallFail = false;

  for (const f of files) {
    const inPath = path.join(refDir, f);
    console.log('\n=== Testing', f, '===');
    const compBuf = fs.readFileSync(inPath);

    // Run native wasm inflate9 to produce reference output
    const refOut = path.join(outDir, 'ref_inflate9__' + f + '.out');
    try {
      // ensure refOut is empty before the reference runner appends to it
      try { fs.unlinkSync(refOut); } catch (e) { /* ignore if missing */ }
      fs.writeFileSync(refOut, '');
      const rc = child.spawnSync('node', ['src/wasm/tests/test_inflate9_stream.js', wasmPath, inPath, refOut], { stdio: 'inherit' });
      if (rc.status !== 0) {
        console.error('Reference inflate9 runner failed for', f, 'rc=', rc.status);
        overallFail = true; continue;
      }
    } catch (e) { console.error('failed to run reference runner', e); overallFail = true; continue; }

    // Run DecompressionStream pipeline
    const pump = new TransformStream();
    const writer = pump.writable.getWriter();
  // DecompressionStream will read wasm exports from globalThis.WASM_EXPORTS
  const ds = new DecompressionStream('deflate64-raw');
    const outStream = pump.readable.pipeThrough(ds);
    const reader = outStream.getReader();

    const outChunks = [];
    const readerTask = (async ()=>{
      while (true) {
        const { done, value } = await reader.read();
        if (done) break;
        outChunks.push(Buffer.from(value));
      }
    })();

    const writerTask = (async ()=>{
      const CHUNK = 4096;
      for (let off = 0; off < compBuf.length; off += CHUNK) {
        const slice = compBuf.subarray(off, Math.min(off + CHUNK, compBuf.length));
        await writer.write(slice);
      }
      await writer.close();
    })();

    await Promise.all([readerTask, writerTask]);
    const outBuf = Buffer.concat(outChunks);

    const refBuf = fs.readFileSync(refOut);
    if (outBuf.length !== refBuf.length) {
      console.error('FAIL length mismatch for', f, outBuf.length, 'vs', refBuf.length);
      overallFail = true; continue;
    }
    if (Buffer.compare(outBuf, refBuf) !== 0) {
      console.error('FAIL content mismatch for', f);
      overallFail = true; continue;
    }

    // write the DecompressionStream output for inspection
    const ourOutPath = path.join(outDir, 'decompstream_inflate9__' + f + '.out');
    fs.writeFileSync(ourOutPath, outBuf);
    console.log('PASS', f, 'wrote', ourOutPath, 'size=', outBuf.length);
  }

  if (overallFail) process.exit(3);
  console.log('\nALL PASSED');
  process.exit(0);
})();
