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

#include "parser/unpacker.h"

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

namespace fschema::parser {

  [[nodiscard]] std::expected<std::vector<std::byte>, DecompressError>
    UnpackLitematicFrom(const std::filesystem::path& path) {
    std::error_code ec;
    const auto file_size = std::filesystem::file_size(path, ec);
    if (ec) [[unlikely]] {
      return std::unexpected(DecompressError::FileNotFound);
    }

    if (file_size < 18 || file_size > std::numeric_limits<uint32_t>::max()) [[unlikely]] {
      return std::unexpected(DecompressError::InvalidGzipHeader);
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) [[unlikely]] {
      return std::unexpected(DecompressError::FileNotFound);
    }

    std::vector<std::byte> compressed(file_size);
    if (!file.read(reinterpret_cast<char*>(compressed.data()),
      static_cast<std::streamsize>(file_size))) [[unlikely]] {
      return std::unexpected(DecompressError::FileReadFailed);
    }
    file.close();

    if (compressed[0] != std::byte{ 0x1F } || compressed[1] != std::byte{ 0x8B }) [[unlikely]] {
      return std::unexpected(DecompressError::InvalidGzipHeader);
    }

    libdeflate_decompressor* d = libdeflate_alloc_decompressor();
    if (!d) [[unlikely]] {
      return std::unexpected(DecompressError::ZlibInitFailed);
    }

    struct DeflateGuard {
      libdeflate_decompressor* d;
      ~DeflateGuard() noexcept { if (d) libdeflate_free_decompressor(d); }
    } guard{ d };

    uint32_t isize = 0;
    std::memcpy(&isize, compressed.data() + file_size - 4, 4);

    std::size_t out_cap = isize > 0
      ? static_cast<std::size_t>(isize)
      : static_cast<std::size_t>(file_size) * 4;

    if (out_cap < file_size * 2) {
      out_cap = file_size * 2;
    }

    constexpr std::size_t kHardCap = 1ULL << 30; // 1 GiB LIMIT
    if (out_cap > kHardCap) [[unlikely]] {
      return std::unexpected(DecompressError::DecompressFailed);
    }

    std::vector<std::byte> output(out_cap);

    for (;;) {
      std::size_t actual_out = 0;
      libdeflate_result res = libdeflate_gzip_decompress(
        d, compressed.data(), compressed.size(),
        output.data(), output.size(), &actual_out);

      if (res == LIBDEFLATE_SUCCESS) [[likely]] {
        output.resize(actual_out);
        output.shrink_to_fit();
        return output;
      }

      if (res == LIBDEFLATE_INSUFFICIENT_SPACE) [[unlikely]] {
        if (output.size() >= kHardCap) [[unlikely]] {
          return std::unexpected(DecompressError::DecompressFailed);
        }
        out_cap = std::min(output.size() * 2, kHardCap);
        output.resize(out_cap);
        continue;
      }

      return std::unexpected(DecompressError::DecompressFailed);
    }
  }

} // namespace fschema::parser