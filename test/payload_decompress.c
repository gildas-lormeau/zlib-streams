/*
 * payload_decompress.c
 *
 * Robust harness using inflate9 (the custom inflate implementation). This
 * mirrors the behavior and exit codes of payload_decompress_ref.c so both
 * tests behave identically from a harness perspective.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "zlib.h"

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

/* inflate9 API (from zlib/inflate9.c) */
int ZEXPORT inflate9Init(z_streamp strm);
int ZEXPORT inflate9(z_streamp strm, int flush);
int ZEXPORT inflate9End(z_streamp strm);

/* in/out state */
static unsigned char *inbuf = NULL;
static size_t inbuf_sz = 0;

/* centralized cleanup helper (same shape as ref harness) */
static int cleanup_inflate(z_streamp strm, FILE *outf,
                           unsigned char *inbuf_local,
                           unsigned char *window_local, int call_end, int rc) {
  if (call_end && strm) {
    int endret = inflate9End(strm);
    if (endret != Z_OK)
      fprintf(stderr, "inflate9End returned %d\n", endret);
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

  int init_ret = inflate9Init(&strm);
  if (init_ret != Z_OK) {
    fprintf(stderr, "inflate9Init failed: %d\n", init_ret);
    free(inbuf);
    free(window);
    return EXIT_INIT_FAIL;
  }

  FILE *outf = fopen(outpath, "wb");
  if (!outf) {
    perror("fopen out");
    return cleanup_inflate(&strm, NULL, inbuf, window, 1, EXIT_OUT_OPEN);
  }

  /* provide the whole compressed buffer as input and a 64KiB output window */
  strm.next_in = inbuf;
  strm.avail_in = (unsigned)inbuf_sz;
  strm.next_out = window;
  strm.avail_out = 65536;

  /* Call inflate9 repeatedly until stream end or error. The inflate9
   * implementation may return Z_OK when the provided output buffer is
   * filled; treat that case by writing the produced block and continuing
   * the decomposition loop. This mirrors common streaming usage. */
  int ret = Z_OK;
  while (ret == Z_OK) {
    unsigned avail_before = strm.avail_out;
    /* Use Z_NO_FLUSH for intermediate calls and only use Z_FINISH when we
     * have no more input to provide. This avoids inflate9 converting a
     * successful partial progress into Z_BUF_ERROR when called with
     * Z_FINISH repeatedly. */
    int call_flush = (strm.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;
    ret = inflate9(&strm, call_flush);
    /* produced bytes are the difference in avail_out */
    size_t produced = (size_t)avail_before - (size_t)strm.avail_out;
    if (produced > 0) {
      size_t wrote = fwrite(window, 1, produced, outf);
      if (wrote != produced) {
        perror("fwrite out");
        return cleanup_inflate(&strm, outf, inbuf, window, 1, EXIT_OUT_OPEN);
      }
    }
    fprintf(stderr, "inflate9 loop ret=%d\n", ret);
    if (ret == Z_OK) {
      /* If output buffer exhausted, reset next_out/avail_out to continue. */
      if (strm.avail_out == 0) {
        strm.next_out = window;
        strm.avail_out = 65536;
      } else {
        /* Z_OK without filling the buffer is unexpected here; break to avoid
         * loop */
        break;
      }
    }
  }
  if (ret != Z_STREAM_END) {
    if (ret == Z_BUF_ERROR) {
      /* treat buffer errors like other decompression failures */
    }
    if (ret == EXIT_OUT_OPEN) {
      fprintf(stderr, "inflate9 aborted: output write error\n");
      return cleanup_inflate(&strm, outf, inbuf, window, 1, EXIT_OUT_OPEN);
    }
    if (strm.msg)
      fprintf(stderr, "inflate9 msg: %s\n", strm.msg);
    return cleanup_inflate(&strm, outf, inbuf, window, 1, EXIT_FAIL_DECOMP);
  }

  int endret = inflate9End(&strm);
  if (endret != Z_OK)
    fprintf(stderr, "inflate9End returned %d\n", endret);

  if (fclose(outf) != 0)
    perror("fclose out");

  return cleanup_inflate(
      &strm, NULL, inbuf, window, 0,
      (ret == Z_STREAM_END && endret == Z_OK) ? 0 : EXIT_FAIL_DECOMP);
}
