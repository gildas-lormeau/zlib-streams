/*
 * payload_decompress_ref.c
 *
 * Robust reference harness using inflateBack9. Reads an entire compressed
 * payload into memory, provides it via a callback to inflateBack9, and
 * writes decompressed output to the provided output file via a callback.
 *
 * This file intentionally mirrors the behavior and exit-code convention
 * used by the other harness (`payload_decompress.c`).  All error paths
 * are handled and return distinct exit codes where appropriate.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "zlib.h"
#include "infback9.h"

/* exit codes (shared convention) */
#define EXIT_FAIL_DECOMP 1
#define EXIT_USAGE 2
#define EXIT_IN_OPEN 3
#define EXIT_NO_MEM_INBUF 4
#define EXIT_INIT_FAIL 5
#define EXIT_NO_MEM_OUTBUF 6
#define EXIT_IN_READ 7
#define EXIT_OUT_OPEN 8

/* expose zlib verbose switch from zutil.c (only present when ZLIB_DEBUG) */
#ifdef ZLIB_DEBUG
extern int z_verbose;
#endif

/* zlib allocation hooks (provided by zlib sources) */
extern voidpf zcalloc(voidpf opaque, unsigned items, unsigned size);
extern void zcfree(voidpf opaque, voidpf address);

/* in/out callback state */
static unsigned char *inbuf = NULL;
static size_t inbuf_sz = 0;
static size_t inpos = 0;

/* return input in fixed-size packets so the ref harness mirrors the test
 * harness packetized consumption and forces inflateBack9 to be called
 * repeatedly for large payloads. */
static unsigned in_cb(void *in_desc, unsigned char FAR **next) {
  (void)in_desc;
  if (inpos >= inbuf_sz) {
    *next = Z_NULL;
    return 0;
  }
  *next = inbuf + inpos;
  unsigned avail = (unsigned)(inbuf_sz - inpos);
  const unsigned packet =
      65536U; /* 64KiB packets to mirror 64KiB output window */
  unsigned give = (avail > packet) ? packet : avail;
  inpos += give;
  return give;
}

static int out_cb(void *out_desc, unsigned char FAR *buf, unsigned len) {
  FILE *f = (FILE *)out_desc;
  if (!f)
    return EXIT_OUT_OPEN;
  size_t wrote = fwrite(buf, 1, len, f);
  if (wrote != len)
    return EXIT_OUT_OPEN;
  return 0;
}

/* centralized cleanup helper: calls inflateBack9End() only when requested,
 * closes output file if provided, frees buffers and returns the provided
 * return code. This avoids duplicating the same cleanup code in multiple
 * branches. */
static int cleanup_inflate(z_stream *strm, FILE *outf,
                           unsigned char *inbuf_local,
                           unsigned char *window_local, int call_end, int rc) {
  if (call_end && strm) {
    int endret = inflateBack9End(strm);
    if (endret != Z_OK)
      fprintf(stderr, "inflateBack9End returned %d\n", endret);
  }
  if (outf)
    fclose(outf);
  free(inbuf_local);
  free(window_local);
  return rc;
}

int main(int argc, char **argv) {
  if (argc != 3) {
    fprintf(stderr, "usage: %s payload_file out_decompressed_file\n", argv[0]);
    return EXIT_USAGE;
  }

  const char *inpath = argv[1];
  const char *outpath = argv[2];

  FILE *inf = fopen(inpath, "rb");
  if (!inf) {
    perror("fopen input");
    return EXIT_IN_OPEN;
  }

  if (fseek(inf, 0, SEEK_END) != 0) {
    perror("fseek");
    fclose(inf);
    return EXIT_IN_READ;
  }
  long lsz = ftell(inf);
  if (lsz < 0) {
    perror("ftell");
    fclose(inf);
    return EXIT_IN_READ;
  }
  if (fseek(inf, 0, SEEK_SET) != 0) {
    perror("fseek");
    fclose(inf);
    return EXIT_IN_READ;
  }

  inbuf_sz = (size_t)lsz;
  inbuf = malloc(inbuf_sz ? inbuf_sz : 1);
  if (!inbuf) {
    fclose(inf);
    return EXIT_NO_MEM_INBUF;
  }

  size_t got = fread(inbuf, 1, inbuf_sz, inf);
  if (got != inbuf_sz) {
    if (ferror(inf))
      perror("fread");
    else
      fprintf(stderr, "short read: got %zu expected %zu\n", got, inbuf_sz);
    free(inbuf);
    fclose(inf);
    return EXIT_IN_READ;
  }
  fclose(inf);
  inpos = 0;

  unsigned char *window = malloc(65536);
  if (!window) {
    free(inbuf);
    return EXIT_NO_MEM_OUTBUF;
  }

  z_stream strm;
  memset(&strm, 0, sizeof(strm));
  strm.zalloc = zcalloc;
  strm.zfree = zcfree;
  strm.opaque = Z_NULL;

#ifdef ZLIB_DEBUG
  z_verbose = 2;
#endif

  int init_ret =
      inflateBack9Init_(&strm, window, ZLIB_VERSION, sizeof(z_stream));
  if (init_ret != Z_OK) {
    fprintf(stderr, "inflateBack9Init_ failed: %d\n", init_ret);
    free(inbuf);
    free(window);
    return EXIT_INIT_FAIL;
  }

  FILE *outf = fopen(outpath, "wb");
  if (!outf) {
    perror("fopen out");
    /* ensure we still call End() to match the other harness cleanup */
    return cleanup_inflate(&strm, NULL, inbuf, window, 1, EXIT_OUT_OPEN);
  }

  int ret = Z_OK;
  /* Call inflateBack9 repeatedly and always print the return value after
   * each call. Using a do/while ensures the body (and the debug print)
   * executes at least once and that we loop when inflateBack9 indicates
   * partial progress via Z_OK. */
  do {
    ret = inflateBack9(&strm, in_cb, NULL, out_cb, outf);
    fprintf(stderr, "inflateBack9 loop ret=%d\n", ret);
  } while (ret == Z_OK);

  if (ret != Z_STREAM_END) {
    /* If the out callback returned a specific exit code, propagate it */
    if (ret == EXIT_OUT_OPEN) {
      fprintf(stderr, "inflateBack9 aborted: output write error\n");
      return cleanup_inflate(&strm, outf, inbuf, window, 1, EXIT_OUT_OPEN);
    }

    fprintf(stderr, "inflateBack9 ret=%d\n", ret);
    if (strm.msg)
      fprintf(stderr, "inflateBack9 msg: %s\n", strm.msg);
    return cleanup_inflate(&strm, outf, inbuf, window, 1, EXIT_FAIL_DECOMP);
  }

  /* decompression reported success; ensure End() also succeeds */
  /* decompression reported success; ensure End() also succeeds */
  int endret = inflateBack9End(&strm);
  if (endret != Z_OK)
    fprintf(stderr, "inflateBack9End returned %d\n", endret);

  /* close and cleanup (End() already called above) */
  if (fclose(outf) != 0)
    perror("fclose out");

  return cleanup_inflate(
      &strm, NULL, inbuf, window, 0,
      (ret == Z_STREAM_END && endret == Z_OK) ? 0 : EXIT_FAIL_DECOMP);
}