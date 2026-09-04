# Sources

`src/zlib` is a submodule of [madler/zlib](https://github.com/madler/zlib) and is never edited. The
files directly under `src/` are forked copies, compiled instead of their submodule counterparts. The
deflate64 work is described in <https://github.com/gildas-lormeau/inflate9.c>.

## Forked from madler/zlib

| file | why it is forked |
|---|---|
| `inflate.c` | decodes raw deflate64 with `inflateInit2(strm, -16)` |
| `inftrees.c`, `inftrees.h` | deflate64 length code 285 and distance codes 30 and 31 |
| `inffast.c` | copy of the submodule file, kept so the fork builds standalone |
| `trees.c` | builds the static deflate trees at run time instead of embedding them |

## Forked from Chromium's zlib

`deflate.c`, `deflate.h`, `compare256.h`, `insert_string.h` and `cpu_features.h` come from
<https://chromium.googlesource.com/chromium/src/third_party/zlib>, revision
`285e94b8fa95ad3b7d16b80798ec8dce6febb8c8`. They are BSD-licensed, see the headers in the files.

The wasm build turns on two of Chromium's options, in `WASM_DEFLATE_CFLAGS`:

- `DEFLATE_COMPARE256_64LE` extends matches 8 bytes at a time in `longest_match`. Same match length,
  computed faster. About 7% at every level.
- `USE_ZLIB_RABIN_KARP_ROLLING_HASH` turns Chromium's own hash off, so the deflate stream stays
  bit-for-bit what madler's `deflate.c` produces. Dropping this flag enables the faster ANZAC hash,
  which is valid DEFLATE but different bytes.

Three deliberate edits to the Chromium files, to re-apply when they are updated:

1. `deflate.c` includes `"insert_string.h"` instead of `"contrib/optimizations/insert_string.h"`,
   because this repo keeps forked zlib sources flat.
2. `deflate.h` leaves `LIT_MEM` off, as madler does. Chromium turns it on, which costs 16 KB of
   `pending_buf` per deflate stream for about 1.5% of speed. The wasm heap is fixed at 16.9 MB, so
   that is 62 concurrent deflate streams instead of 59.
3. `deflate.h` casts the arguments of `zmemcpy` and `zmemzero`. Chromium's `zutil.h` declares them
   taking `void *`, madler's takes `Bytef *`, and this build keeps madler's.

`deflate_crc32.c` supplies `crc_reset`, `crc_finalize` and `copy_with_crc`. Chromium's `crc32.c` owns
them and `deflate.c` calls them on the gzip path, but this build keeps madler's `crc32.c`.
