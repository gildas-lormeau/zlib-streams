// A stream that ends early has to give its buffers and its zlib state back to the wasm heap, which
// never grows and never shrinks, whatever ended it: the consumer cancelling the readable side, the
// producer aborting the writable side, a failing source or sink on either end of a pipe, or the
// codec itself rejecting the input. The wrapper used to free them in the transformer cancel() hook
// of a TransformStream, which Node and Deno call and no browser does, so every abandoned stream
// leaked 128 KB plus its zlib state there and the heap ran out after about a hundred of them.
// The heap holds about 120 such streams, so 100 abandoned streams per direction and per codec
// exhaust it several times over when anything is left behind; the capacity is measured in 64 KB
// blocks before and after, and each codec must still work at the end. Every direction feeds one
// valid stream in pieces and never waits for output a piece may not produce, because an inflater
// past its end consumes nothing, a deflater buffers a compressible chunk whole, and a test that
// hangs on either exits with code 0 once the event loop is empty, hence the watchdog.

import { existsSync, readFileSync } from 'fs';
import { join } from 'path';

const ITERATIONS = 100;
const BLOCK_LENGTH = 65536;
const PIECE_LENGTH = 16384;
const RANDOM_LENGTH = 512 * 1024;
const TIMEOUT = 120000;
const DEFLATE64_PATH = join('test', 'ref-data', '100k_lines.deflate64');

await (async () => {
    const wasmPath = process.argv[2] || join('dist', 'zlib-streams-dev.wasm');
    if (!existsSync(wasmPath)) {
        console.error('wasm not found:', wasmPath);
        process.exit(1);
    }
    if (!existsSync(DEFLATE64_PATH)) {
        console.error('fixture not found:', DEFLATE64_PATH);
        process.exit(1);
    }
    const watchdog = setTimeout(() => {
        console.error('FAIL timed out after ' + TIMEOUT + ' ms, a stream deadlocked');
        process.exit(1);
    }, TIMEOUT);
    const wasmBuf = readFileSync(wasmPath);
    const { instance } = await WebAssembly.instantiate(wasmBuf, { env: { emscripten_notify_memory_growth: () => { } } });
    const exports = instance.exports;
    const mod = await import('../api/zlib-streams.js');
    mod.setWasmExports(exports);
    const failures = [];

    const random = randomBytes(RANDOM_LENGTH);
    const codecs = [
        { name: 'inflate deflate-raw', make: () => new mod.DecompressionStreamZlib('deflate-raw'), input: split(await compress(mod, 'deflate-raw', random)), rejectsGarbage: true },
        { name: 'inflate9 deflate64-raw', make: () => new mod.DecompressionStreamZlib('deflate64-raw'), input: split(new Uint8Array(readFileSync(DEFLATE64_PATH))), rejectsGarbage: true },
        { name: 'deflate deflate-raw', make: () => new mod.CompressionStreamZlib('deflate-raw'), input: split(random, BLOCK_LENGTH), rejectsGarbage: false },
        { name: 'deflate gzip', make: () => new mod.CompressionStreamZlib('gzip'), input: split(random, BLOCK_LENGTH), rejectsGarbage: false }
    ];

    const capacityBefore = measureCapacity(exports);
    console.log(`heap capacity before: ${capacityBefore} blocks of ${BLOCK_LENGTH} bytes`);

    const directions = {
        async 'readable cancelled by the consumer'(codec) {
            const stream = codec.make();
            const writer = stream.writable.getWriter();
            const reader = stream.readable.getReader();
            const pendingRead = reader.read();
            await writer.write(codec.input[0]);
            await reader.cancel(new Error('consumer cancelled'));
            await pendingRead.catch(() => { });
            if (await writer.ready.then(() => 'resolved', () => 'rejected') != 'rejected') {
                return 'writer.ready did not reject after the readable was cancelled';
            }
            if (await writer.write(codec.input[1]).then(() => 'resolved', () => 'rejected') != 'rejected') {
                return 'a write after the readable was cancelled did not reject';
            }
        },
        async 'writable aborted by the producer'(codec) {
            const stream = codec.make();
            const writer = stream.writable.getWriter();
            const reader = stream.readable.getReader();
            const pendingRead = reader.read();
            await writer.write(codec.input[0]);
            await pendingRead.catch(() => { });
            const reason = new Error('producer aborted');
            const aborted = writer.abort(reason).catch(() => { });
            const outcome = await reader.read().then(() => 'resolved', error => error);
            await aborted;
            if (outcome !== reason) {
                return 'a read after the writable was aborted did not reject with the abort reason';
            }
        },
        async 'source erroring under pipeThrough'(codec) {
            let index = 0;
            const reason = new Error('source failed');
            const source = new ReadableStream({
                pull(controller) {
                    if (index < 2) {
                        controller.enqueue(codec.input[index++]);
                    } else {
                        controller.error(reason);
                    }
                }
            });
            const outcome = await source.pipeThrough(codec.make()).pipeTo(new WritableStream({ write() { } })).then(() => 'resolved', error => error);
            if (outcome !== reason) {
                return 'the pipe did not reject with the source error';
            }
        },
        async 'sink erroring under pipeTo'(codec) {
            let index = 0;
            let count = 0;
            const reason = new Error('sink failed');
            const source = new ReadableStream({
                pull(controller) {
                    if (index < codec.input.length) {
                        controller.enqueue(codec.input[index++]);
                    } else {
                        controller.close();
                    }
                }
            });
            const sink = new WritableStream({
                write() {
                    if (++count == 2) {
                        throw reason;
                    }
                }
            });
            const outcome = await source.pipeThrough(codec.make()).pipeTo(sink).then(() => 'resolved', error => error);
            if (outcome !== reason) {
                return 'the pipe did not reject with the sink error, got ' + (outcome && outcome.message || outcome);
            }
        },
        async 'codec rejecting the input'(codec) {
            if (!codec.rejectsGarbage) {
                return;
            }
            const stream = codec.make();
            const writer = stream.writable.getWriter();
            const reader = stream.readable.getReader();
            const pendingRead = reader.read().then(() => 'resolved', error => 'rejected:' + error.message);
            const written = await writer.write(new Uint8Array(PIECE_LENGTH).fill(0xff)).then(() => 'resolved', error => 'rejected:' + error.message);
            if (!written.startsWith('rejected:process error:')) {
                return 'writing garbage did not reject the write with a process error, got ' + written;
            }
            if (!(await pendingRead).startsWith('rejected:process error:')) {
                return 'writing garbage did not reject the pending read with a process error';
            }
        }
    };

    for (const codec of codecs) {
        for (const [direction, run] of Object.entries(directions)) {
            if (direction == 'codec rejecting the input' && !codec.rejectsGarbage) {
                continue;
            }
            let failure;
            try {
                for (let iteration = 0; iteration < ITERATIONS && !failure; iteration++) {
                    failure = await run(codec);
                }
            } catch (error) {
                failure = 'threw ' + error.message;
            }
            if (failure) {
                failures.push(`${codec.name}, ${direction}: ${failure}`);
            } else {
                console.log(`OK ${codec.name}, ${direction} x${ITERATIONS}`);
            }
        }
    }

    // an allocation failure while a stream is being set up must not keep the buffers it did get
    const blocks = [];
    for (const size of [BLOCK_LENGTH, 1024, 16]) {
        let block;
        while ((block = exports.malloc(size))) {
            blocks.push(block);
        }
    }
    try {
        new mod.DecompressionStreamZlib('deflate-raw');
        failures.push('a stream was created on an exhausted heap');
    } catch (error) {
        if (error.message != 'allocation failed') {
            failures.push('unexpected error on an exhausted heap: ' + error.message);
        } else {
            console.log('OK an exhausted heap fails the stream at construction');
        }
    }
    blocks.forEach(block => exports.free(block));

    const capacityAfter = measureCapacity(exports);
    console.log(`heap capacity after: ${capacityAfter} blocks`);
    if (capacityAfter < capacityBefore) {
        failures.push(`the heap lost ${capacityBefore - capacityAfter} blocks of ${BLOCK_LENGTH} bytes`);
    }

    for (const format of ['deflate-raw', 'gzip']) {
        const restored = await decompress(mod, format, await compress(mod, format, random));
        if (!sameBytes(restored, random)) {
            failures.push(`${format} no longer round-trips`);
        }
    }
    const restored = await collect(pieces(codecs[0].input).pipeThrough(codecs[0].make()));
    if (!sameBytes(restored, random)) {
        failures.push('inflate deflate-raw no longer round-trips in pieces');
    }
    const lines = await collect(pieces(codecs[1].input).pipeThrough(codecs[1].make()));
    if (!lines.length) {
        failures.push('inflate9 deflate64-raw no longer decodes the fixture');
    }

    clearTimeout(watchdog);
    if (failures.length) {
        failures.forEach(failure => console.error('FAIL', failure));
        process.exit(1);
    }
    console.log('ALL PASSED');
})();

function randomBytes(length) {
    const bytes = new Uint8Array(length);
    let seed = 0x9e3779b9;
    for (let index = 0; index < length; index++) {
        seed ^= seed << 13;
        seed ^= seed >>> 17;
        seed ^= seed << 5;
        bytes[index] = seed & 0xff;
    }
    return bytes;
}

function split(data, pieceLength = PIECE_LENGTH) {
    const result = [];
    for (let offset = 0; offset < data.length; offset += pieceLength) {
        result.push(data.slice(offset, offset + pieceLength));
    }
    return result;
}

function pieces(input) {
    let index = 0;
    return new ReadableStream({
        pull(controller) {
            if (index < input.length) {
                controller.enqueue(input[index++]);
            } else {
                controller.close();
            }
        }
    });
}

function measureCapacity(exports) {
    const blocks = [];
    let block;
    while ((block = exports.malloc(BLOCK_LENGTH))) {
        blocks.push(block);
    }
    blocks.forEach(block => exports.free(block));
    return blocks.length;
}

function compress(mod, format, data) {
    return collect(pieces(split(data, BLOCK_LENGTH)).pipeThrough(new mod.CompressionStreamZlib(format)));
}

function decompress(mod, format, data) {
    return collect(pieces(split(data)).pipeThrough(new mod.DecompressionStreamZlib(format)));
}

async function collect(readable) {
    const chunks = [];
    let length = 0;
    for await (const chunk of readable) {
        chunks.push(chunk);
        length += chunk.length;
    }
    const result = new Uint8Array(length);
    let offset = 0;
    for (const chunk of chunks) {
        result.set(chunk, offset);
        offset += chunk.length;
    }
    return result;
}

function sameBytes(left, right) {
    return left.length == right.length && left.every((value, index) => value == right[index]);
}
