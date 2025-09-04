#ifndef WASM_STREAM_COMMON_H
#define WASM_STREAM_COMMON_H

#include "zlib.h"
#include "allocator.h"

// Common fields for all WASM stream contexts
#define WASM_STREAM_COMMON_FIELDS                                              \
  z_stream strm;                                                               \
  unsigned char *inbuf;                                                        \
  unsigned inbuf_sz;                                                           \
  unsigned last_consumed;

struct wasm_stream_ctx {
  WASM_STREAM_COMMON_FIELDS;
};

// Common function declarations
unsigned wasm_stream_new(void);
int wasm_stream_end(unsigned zptr, int (*end_func)(z_stream *));
unsigned wasm_stream_last_consumed(unsigned zptr);
int wasm_stream_process_common(unsigned zptr, unsigned in_ptr, unsigned in_len,
                               unsigned out_ptr, unsigned out_len, int flush,
                               int (*process_func)(z_stream *, int));

#endif // WASM_STREAM_COMMON_H
