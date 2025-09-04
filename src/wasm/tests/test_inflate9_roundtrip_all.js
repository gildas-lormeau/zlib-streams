import { mkdirSync, readdirSync, existsSync, statSync } from 'fs';
import { join } from 'path';
import { spawnSync } from 'child_process';
if (process.argv.length < 3) {
    console.error('usage: node test_inflate9_roundtrip_all.js <wasm>');
    process.exit(2);
}
const wasmPath = process.argv[2];
const refDir = join('test', 'ref-data');
const outDir = join('tmp', 'all_runs');
mkdirSync(outDir, { recursive: true });

const files = readdirSync(refDir).filter(f => f.indexOf('deflate64') !== -1);
if (files.length === 0) {
    console.error('no deflate64 payloads found in', refDir);
    process.exit(2);
}

(async () => {
    for (const f of files) {
        const inPath = join(refDir, f);
        const outPath = join(outDir, 'inflate9__' + f + '.out');
        try {
            console.log('\nProcessing', f);
            // reuse the existing test_inflate9_stream.js script to do the heavy lifting
            const rc = spawnSync('node', ['src/wasm/tests/test_inflate9_stream.js', wasmPath, inPath, outPath], { stdio: 'inherit' });
            if (rc.status !== 0) {
                console.error('FAIL:', f, 'rc=', rc.status);
                continue;
            }
            if (existsSync(outPath)) console.log('WROTE', outPath, 'size=', statSync(outPath).size);
        } catch (e) {
            console.error('error processing', f, e && e.stack || e);
        }
    }
    console.log('\nALL DONE');
})();
