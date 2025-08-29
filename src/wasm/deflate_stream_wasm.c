#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "zlib.h"

/* Mirror the RAW_WBITS selection from inflate_stream_wasm.c */
#ifndef RAW_WBITS
#if defined(MAX_WBITS)
#define RAW_WBITS MAX_WBITS
#else
#define RAW_WBITS 15
#endif
#endif

struct wasm_deflate_ctx {
  z_stream strm;
  unsigned char *inbuf;
  unsigned inbuf_sz;
  unsigned last_consumed;
};

unsigned wasm_deflate_new(void) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)malloc(sizeof(*c));
  if (!c)
    return 0;
  memset(c, 0, sizeof(*c));
  c->strm.zalloc = Z_NULL;
  c->strm.zfree = Z_NULL;
  c->strm.opaque = Z_NULL;
  c->inbuf = NULL;
  c->inbuf_sz = 0;
  return (unsigned)(uintptr_t)c;
}

int wasm_deflate_init(unsigned zptr) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  /* default zlib header/compression */
  return deflateInit(&c->strm, Z_DEFAULT_COMPRESSION);
}

/* init with explicit compression level (0-9) */
int wasm_deflate_init_level(unsigned zptr, int level) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  if (level < 0)
    level = Z_DEFAULT_COMPRESSION;
  return deflateInit(&c->strm, level);
}

/* Initialize for raw deflate (no zlib/gzip header). Use -RAW_WBITS to
 * indicate raw deflate to zlib via deflateInit2.
 */
int wasm_deflate_init_raw(unsigned zptr) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  return deflateInit2(&c->strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -RAW_WBITS,
                      8, Z_DEFAULT_STRATEGY);
}

/* raw init with explicit compression level */
int wasm_deflate_init_raw_level(unsigned zptr, int level) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  if (level < 0)
    level = Z_DEFAULT_COMPRESSION;
  return deflateInit2(&c->strm, level, Z_DEFLATED, -RAW_WBITS, 8,
                      Z_DEFAULT_STRATEGY);
}

/* Initialize for gzip-wrapped deflate (zlib will emit gzip header/footer).
 * Use windowBits = MAX_WBITS + 16 to request gzip format.
 */
int wasm_deflate_init_gzip(unsigned zptr) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  return deflateInit2(&c->strm, Z_DEFAULT_COMPRESSION, Z_DEFLATED,
                      MAX_WBITS + 16, 8, Z_DEFAULT_STRATEGY);
}

int wasm_deflate_init_gzip_level(unsigned zptr, int level) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  if (level < 0)
    level = Z_DEFAULT_COMPRESSION;
  return deflateInit2(&c->strm, level, Z_DEFLATED, MAX_WBITS + 16, 8,
                      Z_DEFAULT_STRATEGY);
}

int wasm_deflate_process(unsigned zptr, unsigned in_ptr, unsigned in_len,
                         unsigned out_ptr, unsigned out_len, int flush) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  if (in_len > c->inbuf_sz) {
    unsigned char *nb = (unsigned char *)realloc(c->inbuf, in_len);
    if (!nb)
      return Z_MEM_ERROR;
    c->inbuf = nb;
    c->inbuf_sz = in_len;
  }
  memcpy(c->inbuf, (unsigned char *)(uintptr_t)in_ptr, in_len);
  c->strm.next_in = c->inbuf;
  c->strm.avail_in = in_len;
  c->strm.next_out = (unsigned char *)(uintptr_t)out_ptr;
  c->strm.avail_out = out_len;

  int ret = deflate(&c->strm, flush);
  int produced = (int)(out_len - c->strm.avail_out);
  /* Record how many input bytes were consumed so the host can advance its
   * input pointer precisely.
   */
  c->last_consumed = (unsigned)(in_len - c->strm.avail_in);
  int code = ret & 0xff;
  return (produced & 0x00ffffff) | ((code & 0xff) << 24);
}

int wasm_deflate_end(unsigned zptr) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  int r = deflateEnd(&c->strm);
  free(c->inbuf);
  free(c);
  return r;
}

unsigned wasm_deflate_last_consumed(unsigned zptr) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return 0;
  return c->last_consumed;
}
