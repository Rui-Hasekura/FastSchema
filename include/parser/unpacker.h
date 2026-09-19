// Copyright (C) 2026 Rui - Hasekura <ruihasekura@gmail.com>
// SPDX - License - Identifier: Apache - 2.0
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

#ifndef FSCHEMA_PARSER_UNPACKER_H
#define FSCHEMA_PARSER_UNPACKER_H

#include <cstddef>
#include <expected>
#include <filesystem>
#include <string_view>
#include <vector>

namespace fschema::parser {

  enum class DecompressError {
    FileNotFound,
    FileReadFailed,
    ZlibInitFailed,
    DecompressFailed,
    InvalidGzipHeader
  };

  constexpr std::string_view ToString(DecompressError error) noexcept {
    switch (error) {
    case DecompressError::FileNotFound:        return "Failed to open file: Path does not exist or access denied.";
    case DecompressError::FileReadFailed:      return "Failed to read file content into memory.";
    case DecompressError::ZlibInitFailed:      return "Failed to initialize zlib-ng inflate engine.";
    case DecompressError::DecompressFailed:    return "Zlib-ng decompression failed due to corrupted data.";
    case DecompressError::InvalidGzipHeader:   return "Invalid Gzip header or unsupported compression type.";
    }
    return "Unknown decompression error.";
  }

  [[nodiscard]] std::expected<std::vector<std::byte>, DecompressError>
    UnpackLitematicFrom(const std::filesystem::path& path);

} // namespace fschema::parser

#endif // FSCHEMA_PARSER_UNPACKER_H