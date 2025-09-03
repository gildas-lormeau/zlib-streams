#include <stdlib.h>
#include "zlib.h"

voidpf my_zalloc(voidpf opaque, unsigned items, unsigned size) {
  (void)opaque;
  return malloc(items * size);
}

void my_zfree(voidpf opaque, voidpf ptr) {
  (void)opaque;
  free(ptr);
}
