// An unknown format is rejected with a TypeError when the stream is constructed, as the platform
// CompressionStream and DecompressionStream do, and before the module state is looked at, so a typo
// is reported as a typo whether or not the module is loaded. The driver used to route any other
// string to the zlib wrapper. The default stays "deflate", the declared supportedFormats all
// construct, and deflate64-raw stays a decompression-only format.

import { existsSync, readFileSync } from 'fs';
import { join } from 'path';

const UNSUPPORTED_FORMAT = /^Unsupported format: /;
const NOT_LOADED_MESSAGE = 'WASM module not loaded';
const UNKNOWN_FORMATS = ['inflate', 'zlib', 'DEFLATE', 'deflate-raw ', '', null, 42];

await (async () => {
    const wasmPath = process.argv[2] || join('dist', 'zlib-streams-dev.wasm');
    if (!existsSync(wasmPath)) {
        console.error('wasm not found:', wasmPath);
        process.exit(1);
    }
    const wasmBuf = readFileSync(wasmPath);
    const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: () => { } } });
    const mod = await import('../api/zlib-streams.js');
    const { CompressionStreamZlib, DecompressionStreamZlib } = mod;
    const failures = [];
    mod.setWasmExports(instance.exports);
    for (const format of UNKNOWN_FORMATS) {
        expectUnsupported('compression of ' + JSON.stringify(format), () => new CompressionStreamZlib(format));
        expectUnsupported('decompression of ' + JSON.stringify(format), () => new DecompressionStreamZlib(format));
    }
    expectUnsupported('compression of deflate64-raw', () => new CompressionStreamZlib('deflate64-raw'));
    for (const format of CompressionStreamZlib.supportedFormats) {
        expectConstructed('compression of ' + format, () => new CompressionStreamZlib(format));
    }
    for (const format of DecompressionStreamZlib.supportedFormats) {
        expectConstructed('decompression of ' + format, () => new DecompressionStreamZlib(format));
    }
    expectConstructed('compression with the default format', () => new CompressionStreamZlib());
    expectConstructed('decompression with the default format', () => new DecompressionStreamZlib());
    mod.resetWasmExports();
    expectUnsupported('decompression of "inflate" with the module unloaded', () => new DecompressionStreamZlib('inflate'));
    try {
        new DecompressionStreamZlib('deflate');
        failures.push('decompression of deflate with the module unloaded: constructed');
    } catch (error) {
        if (error.message != NOT_LOADED_MESSAGE) {
            failures.push('decompression of deflate with the module unloaded: ' + describe(error));
        }
    }
    mod.setWasmExports(instance.exports);
    expectConstructed('decompression of deflate once the module is back', () => new DecompressionStreamZlib('deflate'));
    for (const failure of failures) {
        console.error('FAIL ' + failure);
    }
    if (failures.length) {
        process.exit(1);
    }
    console.log('PASS unknown formats rejected, ' + CompressionStreamZlib.supportedFormats.length + ' compression and ' + DecompressionStreamZlib.supportedFormats.length + ' decompression formats construct');

    function expectUnsupported(label, construct) {
        try {
            const stream = construct();
            stream.readable.cancel().catch(() => { });
            failures.push(label + ': constructed instead of throwing');
        } catch (error) {
            if (!(error instanceof TypeError) || !UNSUPPORTED_FORMAT.test(error.message)) {
                failures.push(label + ': ' + describe(error));
            }
        }
    }

    function expectConstructed(label, construct) {
        try {
            const stream = construct();
            stream.readable.cancel().catch(() => { });
        } catch (error) {
            failures.push(label + ': ' + describe(error));
        }
    }
})();

function describe(error) {
    return error ? (error.name + ': ' + error.message) : 'no error';
}
