// create_deflate64.cpp
// Simple generator for Deflate64 payloads. By default it uses the 7z CLI to
// create ZIP files with Deflate64 entries and extracts raw compressed bytes
// from local file headers. If the 7-Zip SDK is available and the build
// defines USE_7ZIP_SDK, the SDK hook `create_zip_with_7zip_sdk` uses a small
// local C wrapper to avoid including large SDK headers in this TU.

#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

#include "generator_log.h"

#ifdef USE_7ZIP_SDK
extern "C" int create_deflate64_sdk(const char *out_path, const char *in_path,
                                    int level);
#endif

static void write_input(const fs::path &p, std::size_t size, uint8_t pattern) {
  std::ofstream os(p, std::ios::binary);
  if (!os)
    throw std::runtime_error("failed to open input file");
  std::vector<char> block(1024, static_cast<char>(pattern));
  while (size >= block.size()) {
    os.write(block.data(), block.size());
    size -= block.size();
  }
  if (size)
    os.write(block.data(), size);
}

static bool create_zip_cli(const fs::path &zip_path,
                           const fs::path &input_path) {
  std::string cmd = "7z a -tzip -mm=Deflate64 \"" + zip_path.string() +
                    "\" \"" + input_path.string() + "\" > /dev/null 2>&1";
  int rc = std::system(cmd.c_str());
  return rc == 0;
}

// No SDK wrapper for ZIPs here. We will either use the 7z CLI or, when
// enabled, call a SDK-backed function that writes a raw deflate64 blob.

static bool extract_first_payload(const fs::path &zip_path,
                                  const fs::path &out_path, int &out_method,
                                  uint32_t &out_comp_size) {
  std::ifstream is(zip_path, std::ios::binary);
  if (!is)
    return false;
  is.seekg(0, std::ios::end);
  std::streamoff sz = is.tellg();
  is.seekg(0, std::ios::beg);
  if (sz <= 0)
    return false;
  std::vector<uint8_t> data((std::size_t)sz);
  is.read(reinterpret_cast<char *>(data.data()), data.size());

  const std::string sig = "PK\x03\x04";
  auto it = std::search(data.begin(), data.end(), sig.begin(), sig.end());
  if (it == data.end())
    return false;
  std::size_t idx = std::distance(data.begin(), it);
  if (idx + 30 > data.size())
    return false;
  auto read_u16 = [&](std::size_t off) -> uint16_t {
    return (uint16_t)data[off] | ((uint16_t)data[off + 1] << 8);
  };
  auto read_u32 = [&](std::size_t off) -> uint32_t {
    return (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8) |
           ((uint32_t)data[off + 2] << 16) | ((uint32_t)data[off + 3] << 24);
  };
  uint16_t method = read_u16(idx + 8);
  uint32_t comp_size = read_u32(idx + 18);
  uint16_t fnlen = read_u16(idx + 26);
  uint16_t exlen = read_u16(idx + 28);
  std::size_t comp_off = idx + 30 + fnlen + exlen;
  if (comp_off + comp_size > data.size())
    return false;
  std::ofstream os(out_path, std::ios::binary);
  if (!os)
    return false;
  os.write(reinterpret_cast<char *>(data.data() + comp_off), comp_size);
  out_method = (int)method;
  out_comp_size = comp_size;
  return true;
}

static void print_usage() {
  std::cerr << "create_deflate64 -o <out.zip> -i <inputfile> [-m <method>]\n";
  std::cerr << "method: name such as Deflate64 (default: Deflate64) or numeric "
               "method id\n";
}

int main(int argc, char **argv) {
  if (argc < 5) {
    print_usage();
    return 1;
  }

  fs::path out_zip;
  fs::path input_file;
  std::string method_name = "Deflate64";

  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    if ((a == "-o" || a == "--out") && i + 1 < argc) {
      out_zip = argv[++i];
    } else if ((a == "-i" || a == "--in") && i + 1 < argc) {
      input_file = argv[++i];
    } else if ((a == "-m" || a == "--method") && i + 1 < argc) {
      method_name = argv[++i];
    } else {
      std::cerr << "unknown arg: " << a << "\n";
      print_usage();
      return 1;
    }
  }

  if (out_zip.empty() || input_file.empty()) {
    print_usage();
    return 1;
  }

  std::vector<uint8_t> methodSequence;
  if (method_name.empty())
    method_name = "Deflate64";
  if (method_name == "Deflate64" || method_name == "deflate64")
    methodSequence.push_back((uint8_t)9);
  else if (method_name == "Deflate" || method_name == "deflate")
    methodSequence.push_back((uint8_t)8);
  else {
    try {
      int v = std::stoi(method_name);
      if (v >= 0 && v <= 255)
        methodSequence.push_back((uint8_t)v);
    } catch (...) {
      std::cerr << "unknown method: " << method_name << "\n";
      return 2;
    }
  }

  // Prepare stable strings for passing to the C SDK wrapper.
  std::string out_zip_s = out_zip.string();
  std::string input_file_s = input_file.string();
  int method_val = 9;
  if (!methodSequence.empty())
    method_val = (int)methodSequence[0];

#ifdef USE_7ZIP_SDK
  // The SDK-backed helper writes a raw compressed stream to the provided
  // output path (not a ZIP). Create the expected payload path and ask the
  // SDK to write the raw .deflate64 blob directly so we can skip ZIP parsing.
  fs::path out_payload_sdk =
      out_zip.parent_path() / (out_zip.stem().string() + ".deflate64");
  int rc_sdk = create_deflate64_sdk(out_payload_sdk.string().c_str(),
                                    input_file_s.c_str(), method_val);
  if (rc_sdk != 0) {
    std::cerr << "create_deflate64_sdk returned rc=" << rc_sdk << "\n";
  }
  bool ok = (rc_sdk == 0);
  // If SDK wrote the payload, set out_zip to a fake ZIP name so later logic
  // writes the expected .deflate64 path. We'll set out_payload to the
  // actual SDK-written file below.
  (void)out_zip_s;
#else
  // Fall back to the 7z CLI if the SDK support wasn't compiled in.
  bool ok = create_zip_cli(out_zip, input_file);
#endif
  if (!ok) {
    std::cerr << "SDK-based creation failed (ensure src/7zip is present and "
                 "Makefile built with -DUSE_7ZIP_SDK)\n";
    return 3;
  }

  fs::path out_payload =
      out_zip.parent_path() / (out_zip.stem().string() + ".deflate64");
  int method = -1;
  uint32_t comp_sz = 0;
#ifdef USE_7ZIP_SDK
  // SDK path wrote the payload directly to out_payload earlier.
  if (!fs::exists(out_payload)) {
    std::cerr << "SDK reported success but payload missing: " << out_payload
              << "\n";
    return 4;
  }
  // We don't currently parse ZIP local headers for method id; report the
  // chosen method value passed to the SDK.
  method = method_val;
  comp_sz = (uint32_t)fs::file_size(out_payload);
  GEN_LOG_DEBUG([&]() {
    std::cerr << "wrote " << out_payload << " method=" << method
              << " comp_size=" << comp_sz << "\n";
  });
#else
  if (!extract_first_payload(out_zip, out_payload, method, comp_sz)) {
    std::cerr << "failed to extract compressed payload from " << out_zip
              << "\n";
    return 4;
  }
  GEN_LOG_DEBUG([&]() {
    std::cerr << "wrote " << out_payload << " method=" << method
              << " comp_size=" << comp_sz << "\n";
  });
#endif
  return 0;
}
