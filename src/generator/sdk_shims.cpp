// sdk_shims.cpp
// Minimal non-invasive shims to satisfy a few missing global symbols when
// linking the 7-Zip SDK on non-Windows platforms. These are small, local
// definitions only used to let the curated SDK subset link and run on
// macOS. They deliberately avoid changes to SDK sources.

#include <cstdlib>
#include <cstring>
#include <ctime>

#include "../7zip/C/7zTypes.h"
#include "../7zip/CPP/Common/MyGuidDef.h"
#include "../7zip/CPP/7zip/IDecl.h"
#include "../7zip/CPP/Common/MyWindows.h"

extern "C" {

static void *Sdk_AlignedAlloc(const ISzAlloc * /*p*/, size_t size) {
  void *ptr = nullptr;
#if defined(_POSIX_C_SOURCE) || defined(_XOPEN_SOURCE)
  if (posix_memalign(&ptr, 64, size) != 0)
    return nullptr;
  return ptr;
#else
  return malloc(size);
#endif
}

static void Sdk_AlignedFree(const ISzAlloc * /*p*/, void *address) {
  free(address);
}

// If the SDK provides its own `g_AlignedAlloc` (via src/7zip/C/Alloc.c) prefer
// that strong symbol; mark our fallback as weak so the linker will pick the
// SDK-provided definition when present.
__attribute__((weak)) ISzAlloc g_AlignedAlloc = {Sdk_AlignedAlloc,
                                                 Sdk_AlignedFree};

// Common stream / coder IIDs used by the SDK subset we compile.
extern const GUID IID_ISequentialInStream = {k_7zip_GUID_Data1,
                                             k_7zip_GUID_Data2,
                                             k_7zip_GUID_Data3_Common,
                                             {0, 0, 0, 3, 0, 0x01, 0, 0}};
extern const GUID IID_ISequentialOutStream = {k_7zip_GUID_Data1,
                                              k_7zip_GUID_Data2,
                                              k_7zip_GUID_Data3_Common,
                                              {0, 0, 0, 3, 0, 0x02, 0, 0}};
extern const GUID IID_IInStream = {k_7zip_GUID_Data1,
                                   k_7zip_GUID_Data2,
                                   k_7zip_GUID_Data3_Common,
                                   {0, 0, 0, 3, 0, 0x03, 0, 0}};
extern const GUID IID_IOutStream = {k_7zip_GUID_Data1,
                                    k_7zip_GUID_Data2,
                                    k_7zip_GUID_Data3_Common,
                                    {0, 0, 0, 3, 0, 0x04, 0, 0}};
extern const GUID IID_IStreamGetSize = {k_7zip_GUID_Data1,
                                        k_7zip_GUID_Data2,
                                        k_7zip_GUID_Data3_Common,
                                        {0, 0, 0, 3, 0, 0x06, 0, 0}};
extern const GUID IID_IOutStreamFinish = {k_7zip_GUID_Data1,
                                          k_7zip_GUID_Data2,
                                          k_7zip_GUID_Data3_Common,
                                          {0, 0, 0, 3, 0, 0x07, 0, 0}};
extern const GUID IID_IStreamGetProps = {k_7zip_GUID_Data1,
                                         k_7zip_GUID_Data2,
                                         k_7zip_GUID_Data3_Common,
                                         {0, 0, 0, 3, 0, 0x08, 0, 0}};
extern const GUID IID_IStreamGetProps2 = {k_7zip_GUID_Data1,
                                          k_7zip_GUID_Data2,
                                          k_7zip_GUID_Data3_Common,
                                          {0, 0, 0, 3, 0, 0x09, 0, 0}};
extern const GUID IID_IStreamGetProp = {k_7zip_GUID_Data1,
                                        k_7zip_GUID_Data2,
                                        k_7zip_GUID_Data3_Common,
                                        {0, 0, 0, 3, 0, 0x0a, 0, 0}};

extern const GUID IID_ICompressCoder = {k_7zip_GUID_Data1,
                                        k_7zip_GUID_Data2,
                                        k_7zip_GUID_Data3_Common,
                                        {0, 0, 0, 4, 0, 0x05, 0, 0}};
extern const GUID IID_ICompressCoder2 = {k_7zip_GUID_Data1,
                                         k_7zip_GUID_Data2,
                                         k_7zip_GUID_Data3_Common,
                                         {0, 0, 0, 4, 0, 0x18, 0, 0}};
extern const GUID IID_ICompressFilter = {k_7zip_GUID_Data1,
                                         k_7zip_GUID_Data2,
                                         k_7zip_GUID_Data3_Common,
                                         {0, 0, 0, 4, 0, 0x40, 0, 0}};
extern const GUID IID_ICompressSetBufSize = {k_7zip_GUID_Data1,
                                             k_7zip_GUID_Data2,
                                             k_7zip_GUID_Data3_Common,
                                             {0, 0, 0, 4, 0, 0x35, 0, 0}};
extern const GUID IID_ICompressInitEncoder = {k_7zip_GUID_Data1,
                                              k_7zip_GUID_Data2,
                                              k_7zip_GUID_Data3_Common,
                                              {0, 0, 0, 4, 0, 0x36, 0, 0}};
extern const GUID IID_ICompressSetCoderProperties = {
    k_7zip_GUID_Data1,
    k_7zip_GUID_Data2,
    k_7zip_GUID_Data3_Common,
    {0, 0, 0, 4, 0, 0x20, 0, 0}};
extern const GUID IID_ICompressSetCoderPropertiesOpt = {
    k_7zip_GUID_Data1,
    k_7zip_GUID_Data2,
    k_7zip_GUID_Data3_Common,
    {0, 0, 0, 4, 0, 0x1F, 0, 0}};
extern const GUID IID_ICompressSetDecoderProperties2 = {
    k_7zip_GUID_Data1,
    k_7zip_GUID_Data2,
    k_7zip_GUID_Data3_Common,
    {0, 0, 0, 4, 0, 0x22, 0, 0}};
extern const GUID IID_ICompressWriteCoderProperties = {
    k_7zip_GUID_Data1,
    k_7zip_GUID_Data2,
    k_7zip_GUID_Data3_Common,
    {0, 0, 0, 4, 0, 0x23, 0, 0}};
extern const GUID IID_ICompressSetInStream = {k_7zip_GUID_Data1,
                                              k_7zip_GUID_Data2,
                                              k_7zip_GUID_Data3_Common,
                                              {0, 0, 0, 4, 0, 0x31, 0, 0}};
extern const GUID IID_ICompressSetOutStream = {k_7zip_GUID_Data1,
                                               k_7zip_GUID_Data2,
                                               k_7zip_GUID_Data3_Common,
                                               {0, 0, 0, 4, 0, 0x32, 0, 0}};
extern const GUID IID_ICompressSetOutStreamSize = {k_7zip_GUID_Data1,
                                                   k_7zip_GUID_Data2,
                                                   k_7zip_GUID_Data3_Common,
                                                   {0, 0, 0, 4, 0, 0x34, 0, 0}};

// Some additional IIDs used by newer compressor interfaces (Deflate decoder)
extern const GUID IID_ICompressGetInStreamProcessedSize = {
    k_7zip_GUID_Data1,
    k_7zip_GUID_Data2,
    k_7zip_GUID_Data3_Common,
    {0, 0, 0, 4, 0, 0x41, 0, 0}};
extern const GUID IID_ICompressReadUnusedFromInBuf = {
    k_7zip_GUID_Data1,
    k_7zip_GUID_Data2,
    k_7zip_GUID_Data3_Common,
    {0, 0, 0, 4, 0, 0x42, 0, 0}};
extern const GUID IID_ICompressSetFinishMode = {k_7zip_GUID_Data1,
                                                k_7zip_GUID_Data2,
                                                k_7zip_GUID_Data3_Common,
                                                {0, 0, 0, 4, 0, 0x43, 0, 0}};

extern const GUID IID_ICryptoProperties = {k_7zip_GUID_Data1,
                                           k_7zip_GUID_Data2,
                                           k_7zip_GUID_Data3_Common,
                                           {0, 0, 0, 4, 0, 0x80, 0, 0}};
extern const GUID IID_ICryptoResetInitVector = {k_7zip_GUID_Data1,
                                                k_7zip_GUID_Data2,
                                                k_7zip_GUID_Data3_Common,
                                                {0, 0, 0, 4, 0, 0x8C, 0, 0}};
extern const GUID IID_ICryptoSetPassword = {k_7zip_GUID_Data1,
                                            k_7zip_GUID_Data2,
                                            k_7zip_GUID_Data3_Common,
                                            {0, 0, 0, 4, 0, 0x90, 0, 0}};

extern const GUID IID_IUnknown = {
    0x00000000,
    0x0000,
    0x0000,
    {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};

} // extern "C"

namespace NWindows {
namespace NFile {
namespace NDir {
bool SetDirTime(const char * /*path*/, const struct timespec * /*cTime*/,
                const struct timespec * /*aTime*/,
                const struct timespec * /*mTime*/) {
  return true;
}
} // namespace NDir
} // namespace NFile
} // namespace NWindows
