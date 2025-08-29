const fs = require('fs');
const path = require('path');
if (process.argv.length < 3) {
    console.error('usage: node test_inflate9_roundtrip_all.js <wasm>');
    process.exit(2);
}
const wasmPath = process.argv[2];
const refDir = path.join('test', 'ref-data');
const outDir = path.join('tmp', 'all_runs');
fs.mkdirSync(outDir, { recursive: true });

const files = fs.readdirSync(refDir).filter(f => f.indexOf('deflate64') !== -1);
if (files.length === 0) {
    console.error('no deflate64 payloads found in', refDir);
    process.exit(2);
}

(async () => {
    for (const f of files) {
        const inPath = path.join(refDir, f);
        const outPath = path.join(outDir, 'inflate9__' + f + '.out');
        try {
            console.log('\nProcessing', f);
            // reuse the existing test_inflate9_stream.js script to do the heavy lifting
            const spawn = require('child_process').spawnSync;
            const rc = spawn('node', ['src/wasm/tests/test_inflate9_stream.js', wasmPath, inPath, outPath], { stdio: 'inherit' });
            if (rc.status !== 0) {
                console.error('FAIL:', f, 'rc=', rc.status);
                continue;
            }
            if (fs.existsSync(outPath)) console.log('WROTE', outPath, 'size=', fs.statSync(outPath).size);
        } catch (e) {
            console.error('error processing', f, e && e.stack || e);
        }
    }
    console.log('\nALL DONE');
})();
