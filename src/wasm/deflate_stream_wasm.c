#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "zlib.h"
#include "allocator.h"
#include "wasm_stream_common.h"

#ifndef RAW_WBITS
#if defined(MAX_WBITS)
#define RAW_WBITS MAX_WBITS
#else
#define RAW_WBITS 15
#endif
#endif

struct wasm_deflate_ctx {
  WASM_STREAM_COMMON_FIELDS
};

unsigned deflate_new(void) { return wasm_stream_new(); }

int deflate_init(unsigned zptr, int level) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  if (level < 0)
    level = Z_DEFAULT_COMPRESSION;
  return deflateInit(&c->strm, level);
}

int deflate_init_raw(unsigned zptr, int level) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  if (level < 0)
    level = Z_DEFAULT_COMPRESSION;
  return deflateInit2(&c->strm, level, Z_DEFLATED, -RAW_WBITS, 8,
                      Z_DEFAULT_STRATEGY);
}

int deflate_init_gzip(unsigned zptr, int level) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  if (level < 0)
    level = Z_DEFAULT_COMPRESSION;
  return deflateInit2(&c->strm, level, Z_DEFLATED, MAX_WBITS + 16, 8,
                      Z_DEFAULT_STRATEGY);
}

int deflate_process(unsigned zptr, unsigned in_ptr, unsigned in_len,
                    unsigned out_ptr, unsigned out_len, int flush) {
  return wasm_stream_process_common(zptr, in_ptr, in_len, out_ptr, out_len,
                                    flush, deflate);
}

int deflate_end(unsigned zptr) {
  struct wasm_deflate_ctx *c = (struct wasm_deflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  int r = deflateEnd(&c->strm);
  free(c->inbuf);
  free(c);
  return r;
}

unsigned deflate_last_consumed(unsigned zptr) {
  return wasm_stream_last_consumed(zptr);
}
