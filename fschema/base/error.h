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

/// Represents a failure during the parsing or decoding of a schematic file.
///
/// The library uses `std::expected` for error propagation
/// instead of C++ exceptions.
/// All decoding and manipulation functions return `ParseResult<T>`,
/// which is an alias for `std::expected<T, ParseError>`.
struct ParseError {
  /// Categorizes the specific type of parsing or structural failure.
  enum class Code : std::uint8_t {
    // NBT Layer Errors

    // Input buffer ended unexpectedly before parsing completed.
    Truncated,
    // Encountered an NBT tag ID > 12 (kMaxTagId).
    InvalidTagId,
    // A length-prefixed array/string declared a negative size.
    NegativeLength,
    // NBT nesting depth exceeded `DecodeLimits::max_nbt_depth`.
    DepthLimitExceeded,
    // A payload size exceeded the corresponding `DecodeLimits` threshold.
    OversizedPayload,

        // Litematic / IR Layer Errors

    // File format version is outside the supported range
    // (e.g., Litematica < 5 or > 7).
    UnsupportedVersion,
    // A required field was not found in the NBT compound.
    MissingField,
    // NBT tag structure or dimensions violate the format specification.
    InvalidStructure,
    // The LongArray payload is too small for the given volume and
    // bits-per-block.
    BlockStatesTooSmall,
    // A decoded block index points outside the palette bounds.
    PaletteIndexOutOfRange,
    // Width * Height * Length calculation overflowed the 64-bit limit.
    VolumeOverflow,

    // Schem Layer Errors

    // A varint sequence exceeded 5 bytes (32-bit limit) or was malformed.
    VarintOverflow,
    // The BlockData ByteArray is smaller than the expected volume.
    BlockDataTooSmall,
    // A palette index in the Sponge format was negative.
    NegativeIndex,
  };

  /// The specific error category.
  Code code;

  /// A breadcrumb path indicating where in the NBT tree the error occurred
  /// (e.g., "Regions/main/BlockStates"). May be empty for low-level errors.
  std::string path;

  /// The byte offset in the source buffer where the error was detected.
  std::size_t offset;

  /// Constructs a ParseError with the given code, path, and offset.
  [[nodiscard]] static ParseError At(Code code, std::string path, std::size_t offset);
};

/// Convenience alias for functions that return a value or a ParseError.
/// Used extensively throughout the parsing and IR manipulation API.
template <typename T>
using ParseResult = std::expected<T, ParseError>;

/// Converts a ParseError::Code to a static string view (e.g., for logging).
[[nodiscard]] std::string_view ToString(ParseError::Code code) noexcept;

/// Overload for converting a ParseError directly to a string view.
[[nodiscard]] inline std::string_view ToString(const ParseError& e) noexcept {
  return ToString(e.code);
}

}  // namespace fschema

#endif  // FSCHEMA_BASE_ERROR_H_