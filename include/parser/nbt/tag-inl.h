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

#ifndef FSCHEMA_PARSER_NBT_TAG_INL_H_
#define FSCHEMA_PARSER_NBT_TAG_INL_H_

#include "parser/nbt/tag.h"

#include <cstddef>
#include <cstdint>

namespace fschema::parser::nbt {

  [[nodiscard]] constexpr bool IsValidTagType(std::uint8_t id) noexcept {
    return id <= kMaxTagId;
  }

  [[nodiscard]] constexpr std::size_t FixedPayloadSize(TagType tag_type) noexcept {
    switch (tag_type) {
    case TagType::Byte:
      return 1;
    case TagType::Short:
      return 2;
    case TagType::Int:
      return 4;
    case TagType::Long:
      return 8;
    case TagType::Float:
      return 4;
    case TagType::Double:
      return 8;
    default:
      return 0;
    }
  }

  [[nodiscard]] constexpr bool IsScalar(TagType tag_type) noexcept {
    return FixedPayloadSize(tag_type) > 0;
  }

  [[nodiscard]] constexpr std::size_t ElementSize(TagType tag_type) noexcept {
    switch (tag_type) {
    case TagType::ByteArray:
      return 1;
    case TagType::IntArray:
      return 4;
    case TagType::LongArray:
      return 8;
    default:
      return 0;
    }
  }

  [[nodiscard]] constexpr bool IsArray(TagType tag_type) noexcept {
    return ElementSize(tag_type) > 0;
  }

  // Compound/List header size: TagID(1) + int32 len(4) = 5
  [[nodiscard]] constexpr std::size_t ListHeaderSize() noexcept {
    return 5;
  }

} // namespace fschema::parser::nbt

#endif // FSCHEMA_PARSER_NBT_TAG_INL_H_