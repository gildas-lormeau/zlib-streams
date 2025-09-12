#include "zutil.h"

int ZEXPORT inflate9Init(z_streamp strm);
int ZEXPORT inflate9(z_streamp strm, int flush);
int ZEXPORT inflate9End(z_streamp strm);
int ZEXPORT inflate9Init_(z_streamp strm, const char *version, int stream_size);
int ZEXPORT inflate9Init2_(z_streamp strm, int windowBits, const char *version,
                           int stream_size);
int ZEXPORT inflate9Reset2(z_streamp strm, int windowBits);