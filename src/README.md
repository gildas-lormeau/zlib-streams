# Sources

`src/zlib` is a submodule of [madler/zlib](https://github.com/madler/zlib) and is never edited. The
files directly under `src/` are forked copies, compiled instead of their submodule counterparts. The
deflate64 work is described in <https://github.com/gildas-lormeau/inflate9.c>.

## Forked from madler/zlib

| file | why it is forked |
|---|---|
| `inftrees.c`, `inftrees.h` | deflate64 length code 285 and distance codes 30 and 31 |
| `trees.c` | builds the static deflate trees at run time instead of embedding them |

## Forked from Chromium's zlib

`deflate.c`, `deflate.h`, `compare256.h`, `insert_string.h`, `cpu_features.h`, `inflate.c`,
`inffast.h`, `inffast_chunk.c`, `inffast_chunk.h` and `chunkcopy.h` come from
<https://chromium.googlesource.com/chromium/src/third_party/zlib>, revision
`285e94b8fa95ad3b7d16b80798ec8dce6febb8c8`. They are BSD-licensed, see the headers in the files.
`inflate.c`, `inffast_chunk.*` and `chunkcopy.h` are their `contrib/optimizations` versions.

`inflate.c` still carries the deflate64 support on top. That merge is small because the two are
orthogonal: deflate64 never enters `inflate_fast`, so the chunked fast path does not know about it.

### Build options

Deflate, in `WASM_DEFLATE_CFLAGS`:

- `DEFLATE_COMPARE256_64LE` extends matches 8 bytes at a time in `longest_match`. Same match length,
  computed faster. About 7% at every level.
- `USE_ZLIB_RABIN_KARP_ROLLING_HASH` turns Chromium's own hash off, so the deflate stream stays
  bit-for-bit what madler's `deflate.c` produces. Dropping this flag enables the faster ANZAC hash,
  which is valid DEFLATE but different bytes.

Inflate, in `INFLATE_CHUNK_CFLAGS`, needed by every target that builds `inflate.c`, not only the
wasm one:

- `INFLATE_CHUNK_GENERIC` selects the portable chunk copies. `chunkcopy.h` has no wasm SIMD path and
  `#error`s without one of its three arch macros. `-msimd128` was measured and buys nothing.
- `INFLATE_CHUNK_READ_64LE` buffers the input bits in a `uint64_t` inside `inffast_chunk.c` only.
  `state->hold` stays `unsigned long`. Together they are worth about 20%.

### Deliberate edits, to re-apply when the Chromium files are updated

1. `deflate.c`, `inflate.c` and `inffast_chunk.c` include `"insert_string.h"`, `"inffast_chunk.h"`
   and `"chunkcopy.h"` directly, not through `contrib/optimizations/`, because this repo keeps
   forked zlib sources flat.
2. `deflate.h` leaves `LIT_MEM` off, as madler does. Chromium turns it on, which costs 16 KB of
   `pending_buf` per deflate stream for about 1.5% of speed. The wasm heap is fixed at 16.9 MB, so
   that is 62 concurrent deflate streams instead of 59.
3. `deflate.h` casts the arguments of `zmemcpy` and `zmemzero`. Chromium's `zutil.h` declares them
   taking `void *`, madler's takes `Bytef *`, and this build keeps madler's.
4. `inflate.c` keeps the 9-bit and 6-bit root tables instead of Chromium's 10 and 9. Theirs need
   `ENOUGH_LENS` raised from 852 to 1332, which is 1920 more bytes per inflate state and a fifth of
   the concurrent inflate streams, for about 1% of speed. Without that change to `inftrees.h` they
   fail every dynamic block with "invalid literal/lengths set".
5. `chunkcopy.h` includes `<stddef.h>` for `ptrdiff_t`, which `-DZ_SOLO` keeps `zutil.h` from
   pulling in.

`deflate_crc32.c` supplies `crc_reset`, `crc_finalize` and `copy_with_crc`. Chromium's `crc32.c` owns
them and `deflate.c` calls them on the gzip path, but this build keeps madler's `crc32.c`.
