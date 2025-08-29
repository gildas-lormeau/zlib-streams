/*
 * payload_decompress_nowindow.c
 *
 * Similar to payload_decompress.c but does not allocate or provide an
 * external 64KiB window. This exercises inflate9's internal window
 * allocation (updatewindow) and related code paths.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include "zlib.h"

/* reuse same exit codes as payload_decompress.c */
#define EXIT_FAIL_DECOMP 1
#define EXIT_USAGE 2
#define EXIT_IN_OPEN 3
#define EXIT_NO_MEM_INBUF 4
#define EXIT_INIT_FAIL 5
#define EXIT_IN_READ 7
#define EXIT_OUT_OPEN 8

extern voidpf zcalloc(voidpf opaque, unsigned items, unsigned size);
extern void zcfree(voidpf opaque, voidpf address);

int ZEXPORT inflate9Init(z_streamp strm);
int ZEXPORT inflate9(z_streamp strm, int flush);
int ZEXPORT inflate9End(z_streamp strm);

static unsigned char *inbuf = NULL;
static size_t inbuf_sz = 0;

static int cleanup_inflate(z_streamp strm, unsigned char *inb, int call_end,
                           int rc) {
  if (call_end && strm) {
    int endret = inflate9End(strm);
    if (endret != Z_OK)
      fprintf(stderr, "inflate9End returned %d\n", endret);
  }
  free(inb);
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
      fprintf(stderr, "short read\n");
    free(inbuf);
    fclose(inf);
    return EXIT_IN_READ;
  }
  fclose(inf);

  z_stream strm;
  memset(&strm, 0, sizeof(strm));
  strm.zalloc = zcalloc;
  strm.zfree = zcfree;
  strm.opaque = Z_NULL;

  int init_ret = inflate9Init(&strm);
  if (init_ret != Z_OK) {
    fprintf(stderr, "inflate9Init failed: %d\n", init_ret);
    free(inbuf);
    return EXIT_INIT_FAIL;
  }

  FILE *outf = fopen(outpath, "wb");
  if (!outf) {
    perror("fopen out");
    return cleanup_inflate(&strm, inbuf, 1, EXIT_OUT_OPEN);
  }

  /* Do NOT allocate or provide a window. Provide input buffer only. */
  strm.next_in = inbuf;
  strm.avail_in = (unsigned)inbuf_sz;

  int ret = Z_OK;
  /* We'll supply a small temporary out buffer and write produced bytes when
   * available */
  unsigned char tmpout[65536];
  while (ret == Z_OK) {
    unsigned before = strm.avail_out;
    /* set the out buffer for this call: allow inflate9 to allocate its own
     * window; but still supply an out buffer so we can collect produced bytes
     */
    strm.next_out = tmpout;
    strm.avail_out = sizeof(tmpout);
    int call_flush = (strm.avail_in == 0) ? Z_FINISH : Z_NO_FLUSH;
    ret = inflate9(&strm, call_flush);
    unsigned produced = (unsigned)(sizeof(tmpout) - strm.avail_out);
    if (produced) {
      size_t wrote = fwrite(tmpout, 1, produced, outf);
      if (wrote != produced) {
        perror("fwrite out");
        return cleanup_inflate(&strm, inbuf, 1, EXIT_OUT_OPEN);
      }
    }
    fprintf(stderr, "inflate9 nowindow loop ret=%d produced=%u\n", ret,
            produced);
    if (ret == Z_OK) {
      if (strm.avail_out == 0) {
        /* continue */
      } else {
        break;
      }
    }
  }

  if (ret != Z_STREAM_END) {
    if (strm.msg)
      fprintf(stderr, "inflate9 msg: %s\n", strm.msg);
    return cleanup_inflate(&strm, inbuf, 1, EXIT_FAIL_DECOMP);
  }

  int endret = inflate9End(&strm);
  if (endret != Z_OK)
    fprintf(stderr, "inflate9End returned %d\n", endret);
  if (fclose(outf) != 0)
    perror("fclose out");
  return cleanup_inflate(
      &strm, inbuf, 0,
      (ret == Z_STREAM_END && endret == Z_OK) ? 0 : EXIT_FAIL_DECOMP);
}
