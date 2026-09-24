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

#include "fschema/base/decompression.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <expected>
#include <filesystem>
#include <fstream>
#include <limits>
#include <system_error>
#include <vector>

#include <libdeflate.h>

namespace fschema::base {

  [[nodiscard]] std::expected<std::vector<std::byte>, DecompressError>
    DecompressGzipFile(const std::filesystem::path& path) {
    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) [[unlikely]] {
      return std::unexpected(DecompressError::kFileNotFound);
    }

    if (file_size < 18 ||
      file_size > std::numeric_limits<std::uint32_t>::max()) [[unlikely]] {
      return std::unexpected(DecompressError::kInvalidGzipHeader);
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) [[unlikely]] {
      return std::unexpected(DecompressError::kFileNotFound);
    }

    std::vector<std::byte> compressed(file_size);
    if (!file.read(reinterpret_cast<char*>(compressed.data()),
      static_cast<std::streamsize>(file_size))) [[unlikely]] {
      return std::unexpected(DecompressError::kFileReadFailed);
    }
    file.close();

    if (compressed[0] != std::byte{ 0x1F } ||
      compressed[1] != std::byte{ 0x8B }) [[unlikely]] {
      return std::unexpected(DecompressError::kInvalidGzipHeader);
    }

    libdeflate_decompressor* d = libdeflate_alloc_decompressor();
    if (d == nullptr) [[unlikely]] {
      return std::unexpected(DecompressError::kLibdeflateInitFailed);
    }

    struct DeflateGuard {
      libdeflate_decompressor* d;
      ~DeflateGuard() noexcept {
        if (d != nullptr) libdeflate_free_decompressor(d);
      }
    } guard{ d };

    std::uint32_t isize = 0;
    std::memcpy(&isize, compressed.data() + file_size - 4, 4);

    std::size_t out_cap = isize > 0
      ? static_cast<std::size_t>(isize)
      : static_cast<std::size_t>(file_size) * 4;

    if (out_cap < file_size * 2) {
      out_cap = file_size * 2;
    }

    constexpr std::size_t kHardCap = 1ULL << 30;  // 1 GiB limit
    if (out_cap > kHardCap) [[unlikely]] {
      return std::unexpected(DecompressError::kDecompressFailed);
    }

    std::vector<std::byte> output(out_cap);

    for (;;) {
      std::size_t actual_out = 0;
      libdeflate_result res = libdeflate_gzip_decompress(
        d, compressed.data(), compressed.size(),
        output.data(), output.size(), &actual_out);

      if (res == LIBDEFLATE_SUCCESS) [[likely]] {
        output.resize(actual_out);
        return output;
      }

      if (res == LIBDEFLATE_INSUFFICIENT_SPACE) [[unlikely]] {
        if (output.size() >= kHardCap) [[unlikely]] {
          return std::unexpected(DecompressError::kDecompressFailed);
        }
        out_cap = std::min(output.size() * 2, kHardCap);
        output.resize(out_cap);
        continue;
      }

      return std::unexpected(DecompressError::kDecompressFailed);
    }
  }

}  // namespace fschema::base