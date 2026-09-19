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

#ifndef FSCHEMA_PARSER_NBT_TREE_H_
#define FSCHEMA_PARSER_NBT_TREE_H_

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "parser/nbt/tag.h"

namespace fschema::parser::nbt {

  struct NbtCompound;
  struct NbtList;

  // A variant holding the payload of any possible NBT tag.
  // We use std::unique_ptr for compound/list to avoid variant's recursive size issues.
  using NbtPayload = std::variant<
    std::monostate, // For End tag
    std::int8_t,    // Byte
    std::int16_t,   // Short
    std::int32_t,   // Int
    std::int64_t,   // Long
    float,          // Float
    double,         // Double
    std::vector<std::int8_t>,  // ByteArray
    std::string,               // String
    std::vector<std::int32_t>, // IntArray
    std::vector<std::int64_t>, // LongArray
    std::unique_ptr<NbtCompound>, // Compound
    std::unique_ptr<NbtList>      // List
  >;

  struct NbtTag {
    TagType type = TagType::End;
    std::string name;
    NbtPayload payload;
  };

  struct NbtCompound {
    std::vector<NbtTag> children;
  };

  struct NbtList {
    TagType element_type = TagType::End;
    std::vector<NbtPayload> children; // List elements are unnamed
  };

} // namespace fschema::parser::nbt

#endif // FSCHEMA_PARSER_NBT_TREE_H_