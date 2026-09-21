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

#ifndef FSCHEMA_PARSER_NBT_TREE_H_
#define FSCHEMA_PARSER_NBT_TREE_H_

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "parser/nbt/noinit_allocator.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::nbt {

  struct NbtCompound;
  struct NbtList;

  template <typename T>
  using NbtVector = std::vector<T, NoInitAllocator<T>>;

  using NbtPayload = std::variant<
    std::monostate,
    std::int8_t,
    std::int16_t,
    std::int32_t,
    std::int64_t,
    float,
    double,
    std::span<const std::int8_t>,
    std::string_view,
    std::span<const std::byte>,
    std::unique_ptr<NbtCompound>,
    std::unique_ptr<NbtList>
  >;

  struct NbtTag {
    TagType type = TagType::End;
    std::string_view name;
    NbtPayload payload;
  };

  struct NbtCompound {
    std::vector<NbtTag> children;
  };

  struct NbtList {
    TagType element_type = TagType::End;
    std::vector<NbtPayload> children;
  };

}  // namespace fschema::parser::nbt

#endif  // FSCHEMA_PARSER_NBT_TREE_H_