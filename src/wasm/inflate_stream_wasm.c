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

struct wasm_inflate_ctx {
  WASM_STREAM_COMMON_FIELDS
};

unsigned inflate_new(void) { return wasm_stream_new(); }

int inflate_init(unsigned zptr) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  return inflateInit(&c->strm);
}

int inflate_init_raw(unsigned zptr) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  return inflateInit2(&c->strm, -RAW_WBITS);
}

int inflate_init_gzip(unsigned zptr) {
  struct wasm_inflate_ctx *c = (struct wasm_inflate_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
#if defined(MAX_WBITS)
  return inflateInit2(&c->strm, MAX_WBITS + 16);
#else
  return inflateInit2(&c->strm, RAW_WBITS + 16);
#endif
}

int inflate_process(unsigned zptr, unsigned in_ptr, unsigned in_len,
                    unsigned out_ptr, unsigned out_len, int flush) {
  return wasm_stream_process_common(zptr, in_ptr, in_len, out_ptr, out_len,
                                    flush, inflate);
}

int inflate_end(unsigned zptr) { return wasm_stream_end(zptr, inflateEnd); }

unsigned inflate_last_consumed(unsigned zptr) {
  return wasm_stream_last_consumed(zptr);
}
