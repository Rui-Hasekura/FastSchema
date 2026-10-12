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

#ifndef FSCHEMA_BASE_NBT_TAG_H_
#define FSCHEMA_BASE_NBT_TAG_H_

#include <cstdint>

namespace fschema::base {

enum class TagType : std::uint8_t {
  kEnd = 0,
  kByte = 1,
  kShort = 2,
  kInt = 3,
  kLong = 4,
  kFloat = 5,
  kDouble = 6,
  kByteArray = 7,
  kString = 8,
  kList = 9,
  kCompound = 10,
  kIntArray = 11,
  kLongArray = 12,
};

constexpr std::uint8_t kMaxTagId = 12;

[[nodiscard]] constexpr bool IsValidTagType(std::uint8_t id) noexcept;

[[nodiscard]] constexpr std::size_t FixedPayloadSize(TagType tag_type) noexcept;

[[nodiscard]] constexpr bool IsScalar(TagType tag_type) noexcept;

[[nodiscard]] constexpr std::size_t ElementSize(TagType tag_type) noexcept;

[[nodiscard]] constexpr bool IsArray(TagType tag_type) noexcept;

// Compound/List header size: TagID(1) + int32 len(4) = 5
[[nodiscard]] constexpr std::size_t ListHeaderSize() noexcept;

}  // namespace fschema::base

#include "fschema/base/nbt/tag-inl.h"

#endif  // FSCHEMA_BASE_NBT_TAG_H_