// Regression tests for the case where the output buffer fills before the codec
// has consumed any input. The transform loop used to read that as "no more
// progress" and abandon the rest of the chunk.
//
// Two ways to reach it:
//  - a deflate64 match longer than the output buffer (max 65538 vs 64 KB) that
//    starts exactly on an output-buffer boundary, which is the default-buffer
//    case and the one that affected zip.js
//  - any outBuffer smaller than the 32 KB input slice the loop feeds
//
// No available encoder emits deflate64 length code 285: 7-Zip's Deflate64
// encoder caps matches at 257 bytes (DeflateEncoder.cpp, kMatchMaxLen64), so
// the stream below is built by hand.

import { existsSync, readFileSync } from 'fs';
import { join } from 'path';

class BitWriter {
    constructor() {
        this.bytes = [];
        this.bit = 0;
        this.acc = 0;
    }
    writeBits(value, count) {
        for (let index = 0; index < count; index++) {
            this.acc |= ((value >>> index) & 1) << this.bit;
            if (++this.bit == 8) {
                this.bytes.push(this.acc);
                this.acc = 0;
                this.bit = 0;
            }
        }
    }
    // huffman codes are packed most significant bit first
    writeCode(code, count) {
        let reversed = 0;
        for (let index = 0; index < count; index++) {
            reversed |= ((code >>> (count - 1 - index)) & 1) << index;
        }
        this.writeBits(reversed, count);
    }
    finish() {
        if (this.bit) {
            this.bytes.push(this.acc);
        }
        return Buffer.from(this.bytes);
    }
}

const OUT_BUFFER = 64 * 1024;
const TRAILING_LITERALS = 120000;

function buildDeflate64LongMatch() {
    const writer = new BitWriter();
    // final block, fixed huffman codes
    writer.writeBits(1, 1);
    writer.writeBits(1, 2);
    const literal = byte => writer.writeCode(0x30 + byte, 8);
    // length code 285 carries 16 extra bits over base 3 in deflate64
    const match = length => {
        writer.writeCode(0xc0 + 285 - 280, 8);
        writer.writeBits(length - 3, 16);
        writer.writeCode(0, 5);
    };
    // fill the first output buffer exactly, so the maximal match that follows is
    // decoded with no output space left and resumes consuming nothing
    literal(0x41);
    match(OUT_BUFFER - 1);
    match(65538);
    for (let index = 0; index < TRAILING_LITERALS; index++) {
        literal(0x42);
    }
    writer.writeCode(0, 7);
    return { compressed: writer.finish(), expectedLength: 1 + (OUT_BUFFER - 1) + 65538 + TRAILING_LITERALS };
}

async function decompress(DecompressionStreamZlib, format, compressed, options) {
    const stream = new DecompressionStreamZlib(format, options);
    const chunks = [];
    const reader = stream.readable.getReader();
    const readerTask = (async () => {
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(Buffer.from(value));
        }
    })();
    const writer = stream.writable.getWriter();
    await writer.write(new Uint8Array(compressed));
    await writer.close();
    await readerTask;
    return Buffer.concat(chunks);
}

async function compress(CompressionStreamZlib, format, source) {
    const stream = new CompressionStreamZlib(format, { level: 6 });
    const chunks = [];
    const reader = stream.readable.getReader();
    const readerTask = (async () => {
        while (true) {
            const { done, value } = await reader.read();
            if (done) break;
            chunks.push(Buffer.from(value));
        }
    })();
    const writer = stream.writable.getWriter();
    await writer.write(new Uint8Array(source));
    await writer.close();
    await readerTask;
    return Buffer.concat(chunks);
}

(async function () {
    const wasmPath = process.argv[2] || join('dist', 'zlib-streams-dev.wasm');
    if (!existsSync(wasmPath)) {
        console.error('wasm not found at', wasmPath);
        process.exit(2);
    }

    const wasmBuf = readFileSync(wasmPath);
    const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: () => { } } });
    const mod = await import('../api/zlib-streams.js');
    const { CompressionStreamZlib, DecompressionStreamZlib, setWasmExports } = mod;
    setWasmExports(instance.exports);

    const failures = [];

    const { compressed, expectedLength } = buildDeflate64LongMatch();
    try {
        const output = await decompress(DecompressionStreamZlib, 'deflate64-raw', compressed);
        if (output.length !== expectedLength) {
            failures.push(`deflate64 long match: got ${output.length} bytes, expected ${expectedLength}`);
        } else if (!output.subarray(0, OUT_BUFFER).every(byte => byte === 0x41) ||
            !output.subarray(OUT_BUFFER + 65538).every(byte => byte === 0x42)) {
            failures.push('deflate64 long match: output bytes are wrong');
        } else {
            console.log('OK deflate64 match crossing the output buffer boundary');
        }
    } catch (error) {
        failures.push(`deflate64 long match: threw ${error.message}`);
    }

    const source = Buffer.alloc(200000, 0x41);
    const deflated = await compress(CompressionStreamZlib, 'deflate-raw', source);
    for (const outBuffer of [512, 4096, 8192, 32768]) {
        try {
            const output = await decompress(DecompressionStreamZlib, 'deflate-raw', deflated, { outBuffer });
            if (!output.equals(source)) {
                failures.push(`outBuffer ${outBuffer}: got ${output.length} bytes, expected ${source.length}`);
            } else {
                console.log(`OK decompression with outBuffer ${outBuffer}`);
            }
        } catch (error) {
            failures.push(`outBuffer ${outBuffer}: threw ${error.message}`);
        }
    }

    if (failures.length) {
        failures.forEach(failure => console.error('FAIL', failure));
        process.exit(1);
    }
    console.log('ALL PASSED');
})();
