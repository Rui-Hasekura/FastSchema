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

  struct ParseError {
    enum class Code : std::uint8_t {

      // NBT
      Truncated,             // Too short to parse
      InvalidTagId,          // TagID > 12
      NegativeLength,
      DepthLimitExceeded,    // Nested depth too deep
      OversizedPayload,      // Over DecodeLimits

      // Litematic schema
      UnsupportedVersion,    // Version < 5 or > 7
      MissingField,          // Necessary field not found in NBT tree
      BlockStatesTooSmall,   // LongArray is too small for volume * bpb
      PaletteIndexOutOfRange,// idx >= palette.size()
      VolumeOverflow,        // x * y * z overflow

      // Schem schema
      VarintOverflow,
      BlockDataTooSmall,
    };

    Code code;
    std::string path;
    std::size_t offset;   // offset after decompression

    [[nodiscard]] static ParseError At(
      Code code, std::string path, std::size_t offset);
  };

  template <typename T>
  using ParseResult = std::expected<T, ParseError>;

}  // namespace fschema

#endif  // FSCHEMA_BASE_ERROR_H_