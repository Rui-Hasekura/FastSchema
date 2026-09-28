// Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fschema/base/compressor.h"

#include <libdeflate.h>

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <span>
#include <system_error>
#include <vector>

namespace fschema::base {

namespace {

struct DecompressorGuard {
  libdeflate_decompressor* d;
  ~DecompressorGuard() noexcept {
    if (d) libdeflate_free_decompressor(d);
  }
};

struct CompressorGuard {
  libdeflate_compressor* c;
  ~CompressorGuard() noexcept {
    if (c) libdeflate_free_compressor(c);
  }
};

}  // namespace

// DecompressGzip (core)

[[nodiscard]] std::expected<std::vector<std::byte>, CompressorError>
DecompressGzip(std::span<const std::byte> compressed) {
  if (compressed.size() < 18) {
    return std::unexpected(CompressorError::kInvalidGzipHeader);
  }
  if (compressed[0] != std::byte{0x1F} || compressed[1] != std::byte{0x8B}) {
    return std::unexpected(CompressorError::kInvalidGzipHeader);
  }

  libdeflate_decompressor* d = libdeflate_alloc_decompressor();
  if (!d) return std::unexpected(CompressorError::kDecompressorInitFailed);
  DecompressorGuard guard{d};

  std::uint32_t isize = 0;
  std::memcpy(&isize, compressed.data() + compressed.size() - 4, 4);
  if constexpr (std::endian::native == std::endian::big) {
    isize = std::byteswap(isize);
  }

  std::size_t out_cap =
      isize > 0 ? static_cast<std::size_t>(isize) : compressed.size() * 4;
  if (out_cap < compressed.size() * 2) out_cap = compressed.size() * 2;

  constexpr std::size_t kHardCap = 4ULL << 30;
  if (out_cap > kHardCap)
    return std::unexpected(CompressorError::kDecompressFailed);

  std::vector<std::byte> output(out_cap);
  for (;;) {
    std::size_t actual_out = 0;
    libdeflate_result res = libdeflate_gzip_decompress(d,
                                                       compressed.data(),
                                                       compressed.size(),
                                                       output.data(),
                                                       output.size(),
                                                       &actual_out);

    if (res == LIBDEFLATE_SUCCESS) {
      output.resize(actual_out);
      return output;
    }
    if (res == LIBDEFLATE_INSUFFICIENT_SPACE) {
      if (output.size() >= kHardCap)
        return std::unexpected(CompressorError::kDecompressFailed);
      out_cap = std::min(output.size() * 2, kHardCap);
      output.resize(out_cap);
      continue;
    }
    return std::unexpected(CompressorError::kDecompressFailed);
  }
}

// DecompressGzipFile (wrapper)

[[nodiscard]] std::expected<std::vector<std::byte>, CompressorError>
DecompressGzipFile(const std::filesystem::path& path) {
  std::error_code ec;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) [[unlikely]] {
    return std::unexpected(CompressorError::kFileNotFound);
  }
  if (file_size < 18 || file_size > std::numeric_limits<std::uint32_t>::max())
      [[unlikely]] {
    return std::unexpected(CompressorError::kInvalidGzipHeader);
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) [[unlikely]] {
    return std::unexpected(CompressorError::kFileNotFound);
  }

  std::vector<std::byte> compressed(file_size);
  if (!file.read(reinterpret_cast<char*>(compressed.data()),
                 static_cast<std::streamsize>(file_size))) [[unlikely]] {
    return std::unexpected(CompressorError::kFileReadFailed);
  }

  return DecompressGzip(std::span<const std::byte>(compressed));
}

// CompressGzip

[[nodiscard]] std::expected<std::vector<std::byte>, CompressorError>
CompressGzip(std::span<const std::byte> data) {
  libdeflate_compressor* c = libdeflate_alloc_compressor(6);
  if (!c) return std::unexpected(CompressorError::kCompressorInitFailed);
  CompressorGuard guard{c};

  std::size_t bound = libdeflate_gzip_compress_bound(c, data.size());
  std::vector<std::byte> out(bound);

  std::size_t compressed_size = libdeflate_gzip_compress(
      c, data.data(), data.size(), out.data(), out.size());

  if (compressed_size == 0)
    return std::unexpected(CompressorError::kCompressFailed);
  out.resize(compressed_size);
  return out;
}

}  // namespace fschema::base