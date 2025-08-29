#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "zlib.h"

/* Choose RAW_WBITS for raw deflate window selection. If zlib exposes
 * MAX_WBITS use it; otherwise default to 15 (32KiB) for standard zlib.
 * This keeps callers/tests independent of the zlib build-time constant.
 */
#ifndef RAW_WBITS
#if defined(MAX_WBITS)
#define RAW_WBITS MAX_WBITS
#else
#define RAW_WBITS 15
#endif
#endif

struct wasm_inflate_ctx {
  z_stream strm;
  unsigned char *inbuf;
  unsigned inbuf_sz;
  unsigned last_consumed;
};

unsigned wasm_inflate_new(void) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)malloc(sizeof(*c));
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

int wasm_inflate_init(unsigned zptr) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  return inflateInit(&c->strm);
}

/* Initialize for gzip (and zlib) header auto-detection. Use windowBits of
 * MAX_WBITS + 16 so zlib accepts gzip headers as well as zlib headers.
 */
int wasm_inflate_init_gzip(unsigned zptr) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
#if defined(MAX_WBITS)
  return inflateInit2(&c->strm, MAX_WBITS + 16);
#else
  return inflateInit2(&c->strm, RAW_WBITS + 16);
#endif
}

/* Initialize for raw deflate (no zlib/gzip header). Use -MAX_WBITS to
 * indicate raw deflate to zlib. */
int wasm_inflate_init_raw(unsigned zptr) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  return inflateInit2(&c->strm, -RAW_WBITS);
}

int wasm_inflate_process(unsigned zptr, unsigned in_ptr, unsigned in_len,
                         unsigned out_ptr, unsigned out_len, int flush) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
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

  int ret = inflate(&c->strm, flush);
  int produced = (int)(out_len - c->strm.avail_out);
  /* Record how many input bytes were consumed so the host can advance its
   * input pointer precisely.
   */
  c->last_consumed = (unsigned)(in_len - c->strm.avail_in);
  /* Encode produced bytes and zlib return code in a 32-bit value so the
   * JS harness can detect both output and error codes without losing
   * the produced byte count when zlib returns a negative error code.
   */
  int code = ret & 0xff;
  return (produced & 0x00ffffff) | ((code & 0xff) << 24);
}

int wasm_inflate_end(unsigned zptr) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  int r = inflateEnd(&c->strm);
  free(c->inbuf);
  free(c);
  return r;
}

/* Diagnostics: small accessors for JS */
unsigned wasm_inflate_last_consumed(unsigned zptr) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return 0;
  return c->last_consumed;
}
