#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "inflate9.h"
/* Choose DEFLATE64_WBITS for Deflate64 window size selection. This separates
 * the wrapper's decision from the zlib build's MAX_WBITS constant so tests
 * and callers don't have to care whether zlib was configured with 15 or 16
 * bits. If the zlib build already defines MAX_WBITS >= 16, reuse it; else
 * explicitly use 16 (64KiB) which Deflate64 requires.
 */
#ifndef DEFLATE64_WBITS
#if defined(MAX_WBITS) && (MAX_WBITS >= 16)
#define DEFLATE64_WBITS MAX_WBITS
#else
#define DEFLATE64_WBITS 16
#endif
#endif

struct wasm_inflate9_ctx {
  z_stream strm;
  unsigned char *inbuf;
  unsigned inbuf_sz;
  unsigned last_consumed;
};

unsigned wasm_inflate9_new(void) {
  struct wasm_inflate9_ctx *c = (struct wasm_inflate9_ctx *)malloc(sizeof(*c));
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

int wasm_inflate9_init(unsigned zptr) {
  struct wasm_inflate9_ctx *c = (struct wasm_inflate9_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  return inflate9Init(&c->strm);
}

/* Initialize inflate9 for raw deflate (no zlib/gzip header). */
int wasm_inflate9_init_raw(unsigned zptr) {
  struct wasm_inflate9_ctx *c = (struct wasm_inflate9_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  /* Request a raw inflate9 init with a 64KiB window (Deflate64). */
  return inflate9Init2_(&c->strm, -DEFLATE64_WBITS, ZLIB_VERSION,
                        (int)sizeof(z_stream));
}

int wasm_inflate9_process(unsigned zptr, unsigned in_ptr, unsigned in_len,
                          unsigned out_ptr, unsigned out_len, int flush) {
  struct wasm_inflate9_ctx *c = (struct wasm_inflate9_ctx *)(uintptr_t)zptr;
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

  int ret = inflate9(&c->strm, flush);
  int produced = (int)(out_len - c->strm.avail_out);
  /* Record how many input bytes were consumed so the host can advance its
   * input pointer precisely. This prevents the host from unconditionally
   * assuming all supplied bytes were consumed when inflate reports
   * Z_BUF_ERROR or similar.
   */
  c->last_consumed = (unsigned)(in_len - c->strm.avail_in);
  /* Always encode the produced byte count in the low 24 bits and the zlib
   * return code (masked to a byte) in the high byte. This keeps the JS
   * caller able to inspect errors like Z_BUF_ERROR (-5) without the
   * wrapper returning a raw negative integer that loses produced info.
   */
  int code = ret & 0xff;
  return (produced & 0x00ffffff) | ((code & 0xff) << 24);
}

int wasm_inflate9_end(unsigned zptr) {
  struct wasm_inflate9_ctx *c = (struct wasm_inflate9_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  int r = inflate9End(&c->strm);
  free(c->inbuf);
  free(c);
  return r;
}

unsigned wasm_inflate9_last_consumed(unsigned zptr) {
  struct wasm_inflate9_ctx *c = (struct wasm_inflate9_ctx *)(uintptr_t)zptr;
  if (!c)
    return 0;
  return c->last_consumed;
}
