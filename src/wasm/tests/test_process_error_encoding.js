// The *_process exports pack the produced byte count in the low 24 bits and the zlib status code
// in the top byte. The error paths used to return a bare negative int instead, so Z_STREAM_ERROR
// (-2) decoded as 16777214 produced bytes and Z_MEM_ERROR (-4) as 16777212, and the JS API then
// copied that many bytes out of the heap into a 64 KB buffer and died with a RangeError instead of
// reporting the failure. The status is also checked on the compress direction now, which was
// skipped entirely: with the encoding fixed but the check missing, a failed deflate() reports zero
// produced and zero consumed, which the transform loop reads as "done" and drops the rest silently.

import { existsSync, readFileSync } from 'fs';
import { join } from 'path';

function decode(result) {
    const code = (result >> 24) & 0xff;
    return { produced: result & 0x00ffffff, code: (code & 0x80) ? code - 256 : code };
}

(async () => {
    const wasmPath = process.argv[2] || join('dist', 'zlib-streams-dev.wasm');
    if (!existsSync(wasmPath)) {
        console.error('wasm not found:', wasmPath);
        process.exit(1);
    }
    const wasmBuf = readFileSync(wasmPath);
    const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: () => { } } });
    const exports = instance.exports;
    const failures = [];

    // a null stream handle makes every process function take its Z_STREAM_ERROR path
    const out = exports.malloc(65536);
    for (const name of ['deflate_process', 'inflate_process', 'inflate9_process']) {
        const { produced, code } = decode(exports[name](0, 0, 0, out, 65536, 0));
        if (produced !== 0 || code !== -2) {
            failures.push(`${name} with a null handle: produced ${produced}, code ${code}, expected 0 and -2`);
        } else {
            console.log(`OK ${name} reports Z_STREAM_ERROR`);
        }
    }

    // filling the heap makes the realloc of the input buffer inside process() fail
    const { instance: full } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: () => { } } });
    const handle = full.exports.deflate_new();
    if (full.exports.deflate_init_raw(handle, 6) !== 0) {
        failures.push('deflate_init_raw failed');
    }
    const fullOut = full.exports.malloc(65536);
    const fullIn = full.exports.malloc(65536);
    for (const size of [65536, 1024, 16]) {
        while (full.exports.malloc(size)) {
            // eat the rest of the heap
        }
    }
    const { produced, code } = decode(full.exports.deflate_process(handle, fullIn, 32768, fullOut, 65536, 0));
    if (produced !== 0 || code !== -4) {
        failures.push(`deflate_process on an exhausted heap: produced ${produced}, code ${code}, expected 0 and -4`);
    } else {
        console.log('OK deflate_process reports Z_MEM_ERROR');
    }

    // the compress direction used to skip the status check entirely, so the same failure reached the
    // transform loop as "zero produced, zero consumed" and ended the entry without an error
    const { instance: api } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: () => { } } });
    const mod = await import('../api/zlib-streams.js');
    mod.setWasmExports(api.exports);
    const stream = new mod.CompressionStreamZlib('deflate-raw');
    for (const size of [65536, 1024, 16]) {
        while (api.exports.malloc(size)) {
            // eat the heap after the stream allocated its own buffers
        }
    }
    const read = (async () => {
        const reader = stream.readable.getReader();
        while (!(await reader.read()).done) {
            // the codec reports the failure by erroring the stream, which surfaces here
        }
    })();
    try {
        const writer = stream.writable.getWriter();
        writer.write(new Uint8Array(200000)).catch(() => { });
        writer.close().catch(() => { });
        await read;
        failures.push('compressing on an exhausted heap reported no error');
    } catch (error) {
        if (error.message.includes('process error:')) {
            console.log(`OK compressing on an exhausted heap fails with "${error.message}"`);
        } else {
            failures.push(`compressing on an exhausted heap: unexpected error ${error.message}`);
        }
    }

    if (failures.length) {
        failures.forEach(failure => console.error('FAIL', failure));
        process.exit(1);
    }
    console.log('ALL PASSED');
})();
