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

#ifndef FSCHEMA_BASE_ERROR_H_
#define FSCHEMA_BASE_ERROR_H_

#include <cstdint>
#include <expected>
#include <string>

namespace fschema {

// Represents a failure during the parsing or decoding of a schematic file.
// The library uses `std::expected` for error propagation instead of C++
// exceptions. All decoding and manipulation functions return
// `ParseResult<T>`, an alias for `std::expected<T, ParseError>`.
struct ParseError {
  // Categorizes the specific type of parsing or structural failure.
  enum class Code : std::uint8_t {
    // NBT Layer Errors

    // Input buffer ended unexpectedly before parsing completed.
    kTruncated,
    // Encountered an NBT tag ID > 12 (kMaxTagId).
    kInvalidTagId,
    // A length-prefixed array/string declared a negative size.
    kNegativeLength,
    // NBT nesting depth exceeded `DecodeLimits::max_nbt_depth`.
    kDepthLimitExceeded,
    // A payload size exceeded the corresponding `DecodeLimits` threshold.
    kOversizedPayload,

    // Litematica / IR Layer Errors

    // File format version is outside the supported range
    // (e.g., Litematica < 5 or > 7).
    kUnsupportedVersion,
    // A required field was not found in the NBT compound.
    kMissingField,
    // NBT tag structure or dimensions violate the format specification.
    kInvalidStructure,
    // The LongArray payload is too small for the given volume and
    // bits-per-block.
    kBlockStatesTooSmall,
    // A decoded block index points outside the palette bounds.
    kPaletteIndexOutOfRange,
    // Width * Height * Length calculation overflowed the 64-bit limit.
    kVolumeOverflow,

    // Sponge Schematic Layer Errors

    // A varint sequence exceeded 5 bytes (32-bit limit) or was malformed.
    kVarintOverflow,
    // The BlockData ByteArray is smaller than the expected volume.
    kBlockDataTooSmall,
    // A palette index in the Sponge format was negative.
    kNegativeIndex,

        // Geometry Subsystem Errors

    // Resource pack directory missing or assets/minecraft/ not found.
    kResourcePackNotFound,
    // Resource pack's DataVersion is outside the supported range.
    kUnsupportedResourcePackVersion,
    // name+props in the palette cannot be found in the resource pack.
    kUnknownBlockState,
    // Wuffs PNG Decoder returned a non-ok status.
    kTextureDecodeFailed,
    // The texture atlas is too small to fit all textures.
    kAtlasOverflow,
    // Model JSON references a non-existent texture path.
    kModelMissingTexture,
    // Blockstate JSON is malformed or missing.
    kMalformedBlockstateJson,
    // Model JSON structure is invalid.
    kMalformedModelJson,
    // Multipart `when` uses OR/AND combinators that are not supported.
    kUnsupportedMultipart,
    // I/O error occurred while writing GLB.
    kGltfWriteFailed,
  };

  // The specific error category.
  Code code;

  // Breadcrumb path indicating where in the NBT tree the error occurred
  // (e.g., "Regions/main/BlockStates"). Empty for low-level errors.
  std::string path;

  // Byte offset in the source buffer where the error was detected.
  std::size_t offset;

  // Constructs a ParseError with the given code, path, and offset.
  [[nodiscard]] static ParseError At(Code code,
                                     std::string path,
                                     std::size_t offset);
};

// Convenience alias for functions that return a value or a ParseError.
template <typename T>
using ParseResult = std::expected<T, ParseError>;

// Converts a ParseError::Code to a static string view.
[[nodiscard]] std::string_view ToString(ParseError::Code code) noexcept;

// Overload for converting a ParseError directly to a string view.
[[nodiscard]] inline std::string_view ToString(const ParseError& e) noexcept {
  return ToString(e.code);
}

}  // namespace fschema

#endif  // FSCHEMA_BASE_ERROR_H_