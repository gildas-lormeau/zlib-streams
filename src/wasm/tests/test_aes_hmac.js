// The AES-CTR/HMAC-SHA1 engine linked into the zip.js module (dist/zip-module.wasm) next to the
// zlib codecs. The keystream is checked against node's AES-ECB over the WinZip counter blocks, a
// 16-byte little-endian integer starting at 1, far enough for the low byte to carry; the MAC against
// node's HMAC over the ciphertext on both directions; and two interleaved contexts fed uneven
// chunks must match the one-shot results, which is what makes a context per stream safe.

import { existsSync, readFileSync } from 'fs';
import { createCipheriv, createHmac } from 'crypto';

const BLOCK_LENGTH = 16;
const DIGEST_LENGTH = 20;
const KEY_LENGTHS = [16, 24, 32];
const CHUNK_LENGTHS = [65536, 16, 4096, 65536 - 16, 48];

function pattern(length, seed) {
    return Uint8Array.from({ length }, (_, index) => (index * 131 + seed * 17 + (index >> 8)) & 0xff);
}

function keystream(key, length) {
    const blocks = Math.ceil(length / BLOCK_LENGTH);
    const counters = new Uint8Array(blocks * BLOCK_LENGTH);
    for (let block = 0; block < blocks; block++) {
        let value = block + 1;
        for (let index = 0; index < BLOCK_LENGTH && value; index++) {
            counters[block * BLOCK_LENGTH + index] = value & 0xff;
            value = Math.floor(value / 256);
        }
    }
    const cipher = createCipheriv(`aes-${key.length * 8}-ecb`, key, null).setAutoPadding(false);
    return new Uint8Array(cipher.update(counters)).subarray(0, length);
}

function hmac(key, data) {
    return new Uint8Array(createHmac('sha1', key).update(data).digest());
}

function same(first, second) {
    return first.length == second.length && first.every((value, index) => value == second[index]);
}

await (async () => {
    const wasmPath = process.argv[2] || 'dist/zip-module.wasm';
    if (!existsSync(wasmPath)) {
        console.error('wasm not found:', wasmPath);
        process.exit(1);
    }
    const { instance } = await WebAssembly.instantiate(readFileSync(wasmPath), { env: { emscripten_notify_memory_growth: () => { } } });
    const exports = instance.exports;
    const failures = [];
    const buffer = exports.malloc(1 << 20);
    const heap = () => new Uint8Array(exports.memory.buffer);

    function createEngine(key, authKey) {
        const ctx = exports.aes_hmac_new();
        heap().set(key, buffer);
        heap().set(authKey, buffer + key.length);
        const result = exports.aes_hmac_init(ctx, buffer, key.length, buffer + key.length, authKey.length);
        return {
            result,
            process(data, decrypt) {
                heap().set(data, buffer);
                exports.aes_hmac_process(ctx, buffer, data.length, decrypt ? 1 : 0);
                return heap().slice(buffer, buffer + data.length);
            },
            digest() {
                exports.aes_hmac_end(ctx, buffer);
                return heap().slice(buffer, buffer + DIGEST_LENGTH);
            }
        };
    }

    for (const keyLength of KEY_LENGTHS) {
        const key = pattern(keyLength, 3), authKey = pattern(keyLength, 5);
        const length = 300 * BLOCK_LENGTH + 7;
        const plaintext = pattern(length, 7);
        const expectedCiphertext = keystream(key, length).map((value, index) => value ^ plaintext[index]);
        const encrypt = createEngine(key, authKey);
        const ciphertext = encrypt.process(plaintext, false);
        if (!same(ciphertext, expectedCiphertext)) {
            failures.push(`AES-${keyLength * 8} keystream differs from AES-ECB over the counter blocks`);
        }
        if (!same(encrypt.digest(), hmac(authKey, ciphertext))) {
            failures.push(`AES-${keyLength * 8} encryption MAC differs from HMAC-SHA1 over the ciphertext`);
        }
        const decrypt = createEngine(key, authKey);
        if (!same(decrypt.process(ciphertext, true), plaintext)) {
            failures.push(`AES-${keyLength * 8} decryption does not recover the plaintext`);
        }
        if (!same(decrypt.digest(), hmac(authKey, ciphertext))) {
            failures.push(`AES-${keyLength * 8} decryption MAC differs from HMAC-SHA1 over the ciphertext`);
        }
        console.log(`OK AES-${keyLength * 8} keystream and MAC on both directions`);
    }

    const first = { key: pattern(32, 11), authKey: pattern(32, 13), data: pattern(200007, 17) };
    const second = { key: pattern(16, 19), authKey: pattern(16, 23), data: pattern(131073, 29) };
    for (const engine of [first, second]) {
        engine.ciphertext = keystream(engine.key, engine.data.length).map((value, index) => value ^ engine.data[index]);
    }
    const firstEngine = createEngine(first.key, first.authKey);
    const secondEngine = createEngine(second.key, second.authKey);
    const firstOutput = new Uint8Array(first.data.length), secondOutput = new Uint8Array(second.data.length);
    let firstOffset = 0, secondOffset = 0, index = 0;
    while (firstOffset < first.data.length || secondOffset < second.data.length) {
        const length = CHUNK_LENGTHS[index++ % CHUNK_LENGTHS.length];
        if (firstOffset < first.data.length) {
            const chunk = first.data.subarray(firstOffset, firstOffset + length);
            firstOutput.set(firstEngine.process(chunk, false), firstOffset);
            firstOffset += chunk.length;
        }
        if (secondOffset < second.data.length) {
            const chunk = second.ciphertext.subarray(secondOffset, secondOffset + length);
            secondOutput.set(secondEngine.process(chunk, true), secondOffset);
            secondOffset += chunk.length;
        }
    }
    if (!same(firstOutput, first.ciphertext) || !same(firstEngine.digest(), hmac(first.authKey, first.ciphertext))) {
        failures.push('an encrypting context fed uneven chunks while another runs differs from the one-shot result');
    }
    if (!same(secondOutput, second.data) || !same(secondEngine.digest(), hmac(second.authKey, second.ciphertext))) {
        failures.push('a decrypting context fed uneven chunks while another runs differs from the one-shot result');
    }
    console.log('OK two interleaved contexts fed uneven chunks');

    const longKey = new Uint8Array(80).fill(0xaa);
    const longKeyData = new TextEncoder().encode('Test Using Larger Than Block-Size Key - Hash Key First');
    const longKeyEngine = createEngine(pattern(16, 1), longKey);
    longKeyEngine.process(longKeyData, true);
    const longKeyDigest = Buffer.from(longKeyEngine.digest()).toString('hex');
    if (longKeyDigest != 'aa4ae5e15272d00e95705637ce8a3b55ed402112') {
        failures.push(`RFC 2202 case 7 digest ${longKeyDigest}`);
    } else {
        console.log('OK RFC 2202 case 7, a MAC key longer than the SHA-1 block');
    }

    if (createEngine(pattern(20, 1), pattern(20, 1)).result == 0) {
        failures.push('a 20-byte cipher key was accepted');
    } else {
        console.log('OK a cipher key of an invalid length is rejected');
    }

    for (let count = 0; count < 100000; count++) {
        const ctx = exports.aes_hmac_new();
        if (!ctx) {
            failures.push(`context allocation failed after ${count} contexts were freed`);
            break;
        }
        exports.aes_hmac_end(ctx, 0);
    }
    console.log('OK contexts are freed');

    const chunk = pattern(65536, 31);
    const engine = createEngine(pattern(32, 1), pattern(32, 2));
    const total = 32 * 1024 * 1024;
    for (let count = 0; count < 32; count++) {
        engine.process(chunk, false);
    }
    const start = performance.now();
    for (let done = 0; done < total; done += chunk.length) {
        engine.process(chunk, false);
    }
    console.log(`encryption throughput ${(total / 1024 / 1024 / ((performance.now() - start) / 1000)).toFixed(0)} MB/s, copies included`);

    if (failures.length) {
        failures.forEach(failure => console.error('FAIL', failure));
        process.exit(1);
    }
    console.log('ALL PASSED');
})();
