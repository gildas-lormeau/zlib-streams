#include <stdio.h>
#include <stdlib.h>
#include <string.h>
/* Use the project's zlib header */
#include "zlib.h"

/* zlib allocation hooks (provided by zlib sources) */
extern voidpf zcalloc(voidpf opaque, unsigned items, unsigned size);
extern void zcfree(voidpf opaque, voidpf address);

/* Declare inflate9 symbols (from zlib/inflate9.c) */
int ZEXPORT inflate9Init_(z_streamp strm, const char *version, int stream_size);
int ZEXPORT inflate9(z_streamp strm, int flush);
int ZEXPORT inflate9End(z_streamp strm);

int main(int argc, char **argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: %s payload_file\n", argv[0]);
    return 2;
  }
  const char *path = argv[1];
  FILE *f = fopen(path, "rb");
  if (!f) {
    perror("fopen");
    return 3;
  }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  unsigned char *inbuf = malloc(sz + 1);
  if (!inbuf) {
    fclose(f);
    return 4;
  }
  fread(inbuf, 1, sz, f);
  fclose(f);

  z_stream strm;
  memset(&strm, 0, sizeof(strm));
  strm.zalloc = Z_NULL;
  strm.zfree = Z_NULL;
  strm.opaque = Z_NULL;
  strm.zalloc = zcalloc;
  strm.zfree = zcfree;

  if (inflate9Init_(&strm, ZLIB_VERSION, sizeof(z_stream)) != Z_OK) {
    fprintf(stderr, "inflate9Init_ failed\n");
    free(inbuf);
    return 5;
  }

  unsigned char out[512 * 1024];
  strm.next_in = inbuf;
  strm.avail_in = (unsigned)sz;
  strm.next_out = out;
  strm.avail_out = sizeof(out);

  int ret = inflate9(&strm, Z_FINISH);
  if (ret == Z_STREAM_END) {
    printf("OK: %s -> %u bytes\n", path, (unsigned)strm.total_out);
    inflate9End(&strm);
    free(inbuf);
    return 0;
  } else {
    printf("FAIL: %s -> ret=%d total_out=%lu\n", path, ret, strm.total_out);
    inflate9End(&strm);
    free(inbuf);
    return 1;
  }
}
