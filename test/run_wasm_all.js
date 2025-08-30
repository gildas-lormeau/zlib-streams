const fs = require('fs');
const child = require('child_process');
const path = require('path');

const OUTDIR = 'tmp/all_runs';
if (!fs.existsSync(OUTDIR)) fs.mkdirSync(OUTDIR, { recursive: true });
const wasm = 'dist/zlib-streams-dev.wasm';
const refdir = 'test/ref-data';

// If payload paths are provided as arguments, use those. Otherwise use all payloads in refdir.
const args = process.argv.slice(2);
let files;
if (args.length > 0) {
  // assume args are paths or basenames; normalize to full paths
  files = args.map(a => {
    if (fs.existsSync(a) && fs.statSync(a).isFile()) return path.basename(a);
    // if a basename was passed, try refdir
    if (fs.existsSync(path.join(refdir, a))) return a;
    return null;
  }).filter(Boolean);
} else {
  files = fs.readdirSync(refdir).filter(f => f.endsWith('.deflate64'));
}

for (const f of files) {
  const p = path.join(refdir, f);
  console.log('Payload:', f);
  // run inflate (standard) where payloads are raw deflate unless name indicates def64
  // decide whether to run inflate9 vs inflate based on filename contents
  const isDef64 = f.includes('def64') || f.includes('deflate64');
  const runner = isDef64 ? 'src/wasm/tests/test_inflate9_stream.js' : 'src/wasm/tests/test_inflate_stream.js';
  const out = path.join(OUTDIR, `wasm__${f}.out`);
  try {
    child.execFileSync(process.execPath, [runner, wasm, p, out], { stdio: 'inherit' });
    console.log('  wrote', out);
  } catch (e) {
    console.error('  runner failed for', f, e && e.status);
    fs.writeFileSync(path.join(OUTDIR, `wasm__${f}.log`), String(e && e.status || e));
  }
}
console.log('done');
