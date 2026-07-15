#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "zlib.h"
#include "wasm_stream_common.h"

#define DEFLATE64_WBITS 16

struct wasm_inflate9_ctx {
  WASM_STREAM_COMMON_FIELDS;
};

unsigned inflate9_new(void) { return wasm_stream_new(); }

int inflate9_init_raw(unsigned zptr) {
  struct wasm_inflate9_ctx *c = (struct wasm_inflate9_ctx *)(uintptr_t)zptr;
  if (!c)
    return Z_STREAM_ERROR;
  return inflateInit2(&c->strm, -DEFLATE64_WBITS);
}

int inflate9_init(unsigned zptr) { return inflate9_init_raw(zptr); }

int inflate9_process(unsigned zptr, unsigned in_ptr, unsigned in_len,
                     unsigned out_ptr, unsigned out_len, int flush) {
  return wasm_stream_process_common(zptr, in_ptr, in_len, out_ptr, out_len,
                                    flush, inflate);
}

int inflate9_end(unsigned zptr) { return wasm_stream_end(zptr, inflateEnd); }

unsigned inflate9_last_consumed(unsigned zptr) {
  return wasm_stream_last_consumed(zptr);
}
