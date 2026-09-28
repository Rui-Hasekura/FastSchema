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

#ifndef FSCHEMA_BASE_COMPRESSOR_H_
#define FSCHEMA_BASE_COMPRESSOR_H_

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace fschema::base {

enum class CompressorError {
  kFileNotFound,
  kFileReadFailed,
  kDecompressorInitFailed,
  kCompressorInitFailed,
  kDecompressFailed,
  kCompressFailed,
  kInvalidGzipHeader
};

[[nodiscard]] constexpr std::string_view ToString(
    CompressorError error) noexcept {
  switch (error) {
    case CompressorError::kFileNotFound:
      return "Failed to open file: Path does not exist or access denied.";
    case CompressorError::kFileReadFailed:
      return "Failed to read file content into memory.";
    case CompressorError::kDecompressorInitFailed:
      return "Failed to initialize libdeflate decompressor.";
    case CompressorError::kCompressorInitFailed:
      return "Failed to initialize libdeflate compressor.";
    case CompressorError::kDecompressFailed:
      return "libdeflate decompression failed due to corrupted data.";
    case CompressorError::kCompressFailed:
      return "libdeflate compression failed.";
    case CompressorError::kInvalidGzipHeader:
      return "Invalid Gzip header or unsupported compression type.";
  }
  return "Unknown compressor error.";
}

// Reads a gzip-compressed file from disk and decompresses it.
// Reads the file into memory, then delegates to DecompressGzip.
[[nodiscard]] std::expected<std::vector<std::byte>, CompressorError>
DecompressGzipFile(const std::filesystem::path& path);

// Decompresses gzip-compressed data from a byte span.
[[nodiscard]] std::expected<std::vector<std::byte>, CompressorError>
DecompressGzip(std::span<const std::byte> compressed);

// Compresses data to gzip format.
[[nodiscard]] std::expected<std::vector<std::byte>, CompressorError>
CompressGzip(std::span<const std::byte> data);

}  // namespace fschema::base

#endif  // FSCHEMA_BASE_COMPRESSOR_H_