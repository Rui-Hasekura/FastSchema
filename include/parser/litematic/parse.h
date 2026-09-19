/*
 * Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FSCHEMA_PARSER_LITEMATIC_PARSE_H_
#define FSCHEMA_PARSER_LITEMATIC_PARSE_H_

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "parser/error.h"
#include "parser/limits.h"
#include "parser/litematic/detail/root.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"

namespace fschema::parser::litematic {

  // Entry: ParseLitematic after UnpackLitematicFrom
  //
  // bytes' ownership is transferred to the returned Litematic::owner.
  // All internal spans (properties / raw_nbt / preview_data) point to it.
  // This span is valid until the Litematic object is destructed.
  //
  // Precondition: bytes must be a valid gzip decompressed result
  // (guaranteed by the upper layer UnpackLitematicFrom).
  [[nodiscard]] ParseResult<Litematic> ParseLitematic(
    std::unique_ptr<std::vector<std::byte>> bytes,
    const DecodeLimits& limits = {}) {
    if (!bytes) {
      return std::unexpected(ParseError{
          ParseError::Code::Truncated, "", 0 });
    }
    if (bytes->size() > limits.max_decompressed) {
      return std::unexpected(ParseError{
          ParseError::Code::OversizedPayload, "", 0 });
    }

    Litematic result;
    result.owner = std::move(bytes);

    nbt::ByteReader reader(
      std::span<const std::byte>(*result.owner), limits);

    auto parse_result = detail::ParseRoot(reader, result);
    if (!parse_result) {
      return std::unexpected(parse_result.error());
    }

    return result;
  }

} // namespace fschema::parser::litematic

#endif // FSCHEMA_PARSER_LITEMATIC_PARSE_H_