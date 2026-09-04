/* deflate_crc32.c -- gzip CRC-32 helpers for the Chromium deflate.c
 *
 * Chromium's crc32.c owns crc_reset(), crc_finalize() and copy_with_crc(),
 * which deflate.c calls unconditionally on the gzip path. This build keeps
 * madler's crc32.c, so the three functions are provided here without the
 * SIMD folding they carry upstream.
 */

#include "deflate.h"

void ZLIB_INTERNAL crc_reset(deflate_state *const s) {
    s->strm->adler = crc32(0L, Z_NULL, 0);
}

void ZLIB_INTERNAL crc_finalize(deflate_state *const s) {
    (void)s;
}

void ZLIB_INTERNAL copy_with_crc(z_streamp strm, Bytef *dst, long size) {
    zmemcpy(dst, strm->next_in, size);
    strm->adler = crc32(strm->adler, dst, size);
}
