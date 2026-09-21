// Bytes after the end of a stream are rejected, in the write that carries them and on the readable
// side, as the platform DecompressionStream does (Node, Deno and Chrome error on trailing data, a
// second gzip member included). The wrapper used to stop consuming at the end of the stream and drop
// whatever followed, so a caller feeding a stream with a wrong length read the content and never
// learned about it. Every format is covered, the junk arriving in the write that ends the stream and
// in a later write, and a stream ending exactly at its last byte is the control.

import { existsSync, readFileSync } from 'fs';
import { join } from 'path';
import { deflateRawSync, deflateSync, gzipSync } from 'zlib';

const TRAILING_DATA_MESSAGE = 'trailing data after the end of the stream';
const CONTENT_LENGTH = 100000;
const JUNK = new Uint8Array([0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a, 0x5a]);
const DEFLATE64_PATH = join('test', 'ref-data', '100k_lines.deflate64');

await (async () => {
    const wasmPath = process.argv[2] || join('dist', 'zlib-streams-dev.wasm');
    if (!existsSync(wasmPath)) {
        console.error('wasm not found:', wasmPath);
        process.exit(1);
    }
    const wasmBuf = readFileSync(wasmPath);
    const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: () => { } } });
    const mod = await import('../api/zlib-streams.js');
    mod.setWasmExports(instance.exports);
    const failures = [];
    const content = new Uint8Array(CONTENT_LENGTH);
    for (let index = 0; index < content.length; index++) {
        content[index] = index % 7 ? 65 + (index * 7919) % 16 : 10;
    }
    const streams = [
        { format: 'deflate-raw', compressed: new Uint8Array(deflateRawSync(content)), expected: content },
        { format: 'deflate', compressed: new Uint8Array(deflateSync(content)), expected: content },
        { format: 'gzip', compressed: new Uint8Array(gzipSync(content)), expected: content }
    ];
    if (existsSync(DEFLATE64_PATH)) {
        const compressed = new Uint8Array(readFileSync(DEFLATE64_PATH));
        const { output } = await run(mod, 'deflate64-raw', [compressed]);
        streams.push({ format: 'deflate64-raw', compressed, expected: output });
    }
    for (const { format, compressed, expected } of streams) {
        const control = await run(mod, format, [compressed]);
        check(format + ' ending at its last byte', control, expected);
        check(format + ' with junk in the last write', await run(mod, format, [concat(compressed, JUNK)]));
        check(format + ' with junk in a later write', await run(mod, format, [compressed, JUNK]));
        check(format + ' with junk in a later write after an empty one', await run(mod, format, [compressed, new Uint8Array(0), JUNK]));
        check(format + ' followed by a second stream', await run(mod, format, [compressed, compressed]));
        check(format + ' still works after a rejection', await run(mod, format, [compressed]), expected);
    }
    for (const failure of failures) {
        console.error('FAIL ' + failure);
    }
    if (failures.length) {
        process.exit(1);
    }
    console.log('PASS trailing data rejected for ' + streams.map(stream => stream.format).join(', '));

    function check(label, { output, readError, writeError }, expected) {
        if (expected) {
            if (readError || writeError) {
                failures.push(label + ': rejected with ' + describe(readError || writeError));
            } else if (output.length != expected.length || output.some((byte, index) => byte != expected[index])) {
                failures.push(label + ': output differs, ' + output.length + ' bytes for ' + expected.length);
            }
        } else if (!readError || readError.message != TRAILING_DATA_MESSAGE) {
            failures.push(label + ': the readable did not error with the trailing data message, got ' + describe(readError));
        } else if (!writeError || writeError.message != TRAILING_DATA_MESSAGE) {
            failures.push(label + ': the write did not reject with the trailing data message, got ' + describe(writeError));
        }
    }
})();

async function run(mod, format, chunks) {
    const stream = new mod.DecompressionStreamZlib(format);
    const writer = stream.writable.getWriter();
    const writes = (async () => {
        for (const chunk of chunks) {
            await writer.write(chunk);
        }
        await writer.close();
    })().catch(error => error);
    let output, readError;
    try {
        output = new Uint8Array(await new Response(stream.readable).arrayBuffer());
    } catch (error) {
        readError = error;
    }
    return { output, readError, writeError: await writes };
}

function concat(first, second) {
    const result = new Uint8Array(first.length + second.length);
    result.set(first);
    result.set(second, first.length);
    return result;
}

function describe(error) {
    return error ? (error.name + ': ' + error.message) : 'no error';
}
