// sdk_deflate64.cpp
// Create a raw Deflate64 compressed blob using the 7-Zip SDK encoder.
// This file is compiled only when the SDK is present and the build enables
// the SDK-backed generator. It does not create ZIP files — it writes the
// raw compressed stream to the requested output path.

#include <string>
#include <cstdint>
#include <iostream>

#include "generator_log.h"
#include <Common/FileStreams.h>
#include <Common/CreateCoder.h>
#include <ICoder.h>

extern "C" int create_deflate64_sdk(const char *out_path, const char *in_path,
                                    int /*level*/) {
  try {
    CInFileStream inStream;
    if (!inStream.Open(in_path)) {
      std::cerr << "sdk_deflate64: failed to open input: " << in_path << "\n";
      return 2;
    }

    COutFileStream outStream;
    if (!outStream.Create_ALWAYS(out_path)) {
      std::cerr << "sdk_deflate64: failed to create output: " << out_path
                << "\n";
      return 3;
    }

    // determine input size if available
    UInt64 inSize = 0;
    inStream.GetLength(inSize);

    // Instantiate encoder via SDK factory using Deflate64 method id.
    CMethodId def64Id = (CMethodId)0x40109ULL; // Deflate64 per SDK registration
    CCreatedCoder created;
    HRESULT r = CreateCoder_Id(def64Id, true, created);
    if (r != S_OK) {
      std::cerr << "sdk_deflate64: CreateCoder_Id failed HRESULT=" << std::hex
                << r << std::dec << "\n";
      return 5;
    }

    // Prefer single-stream coder
    if (created.Coder) {
      ICompressCoder *coder = created.Coder;
      GEN_LOG_DEBUG(
          [&]() { std::cerr << "sdk_deflate64: using ICompressCoder\n"; });
      HRESULT res =
          coder->Code(&inStream, &outStream,
                      (inSize ? &inSize : (const UInt64 *)NULL), NULL, NULL);
      outStream.Close();
      return (res == S_OK) ? 0 : 4;
    } else if (created.Coder2) {
      GEN_LOG_DEBUG(
          [&]() { std::cerr << "sdk_deflate64: using ICompressCoder2\n"; });
      // fallback to Coder2 path (unlikely for Deflate)
      ICompressCoder2 *coder2 = created.Coder2;
      const UInt64 *inSizes[] = {(inSize ? &inSize : (const UInt64 *)NULL)};
      ISequentialInStream *inStreams[] = {&inStream};
      ISequentialOutStream *outStreams[] = {&outStream};
      HRESULT res =
          coder2->Code(inStreams, inSizes, 1, outStreams, NULL, 1, NULL);
      outStream.Close();
      return (res == S_OK) ? 0 : 4;
    }
    return 6;
  } catch (...) {
    return 1;
  }
}
