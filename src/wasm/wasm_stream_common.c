#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "wasm_stream_common.h"
#include "allocator.h"

unsigned wasm_stream_new(void) {
  struct wasm_stream_ctx *c = (struct wasm_stream_ctx *)malloc(sizeof(*c));
  if (!c)
    return 0;
  memset(c, 0, sizeof(*c));
  c->strm.zalloc = my_zalloc;
  c->strm.zfree = my_zfree;
  c->strm.opaque = Z_NULL;
  c->inbuf = NULL;
  c->inbuf_sz = 0;
  return (unsigned)(uintptr_t)c;
}

int wasm_stream_end(unsigned zptr, int (*end_func)(z_stream *)) {
  struct wasm_stream_ctx *c = (struct wasm_stream_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  int r = end_func(&c->strm);
  free(c->inbuf);
  free(c);
  return r;
}

unsigned wasm_stream_last_consumed(unsigned zptr) {
  struct wasm_stream_ctx *c = (struct wasm_stream_ctx *)(uintptr_t)zptr;
  if (!c)
    return 0;
  return c->last_consumed;
}

int wasm_stream_process_common(unsigned zptr, unsigned in_ptr, unsigned in_len,
                               unsigned out_ptr, unsigned out_len, int flush,
                               int (*process_func)(z_streamp, int)) {
  struct wasm_stream_ctx *c = (struct wasm_stream_ctx *)(uintptr_t)zptr;
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

  int ret = process_func(&c->strm, flush);
  int produced = (int)(out_len - c->strm.avail_out);
  c->last_consumed = (unsigned)(in_len - c->strm.avail_in);
  int code = ret & 0xff;
  return (produced & 0x00ffffff) | ((code & 0xff) << 24);
}
