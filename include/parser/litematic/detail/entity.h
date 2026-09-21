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

#ifndef FSCHEMA_PARSER_LITEMATIC_DETAIL_ENTITY_H_
#define FSCHEMA_PARSER_LITEMATIC_DETAIL_ENTITY_H_

#include <array>
#include <cstddef>
#include <expected>
#include <memory>
#include <vector>

#include "parser/error.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::litematic::detail {

  // Entities: List<Compound>
  //
  // Per Entity Compound's Common Fields
  // (Java written, different entity types have additional fields):
  //   Id: String               <- Entity type ID
  //   Pos: List<Double>(3)     <- World coordinates
  //   Motion: List<Double>(3)  <- Velocity
  //   Rotation: List<Float>(2) <- yaw, pitch
  //   ...remaining fields -> raw_nbt (e.g., Items, Invulnerable, Tags, ...)
  //
  // Common fields are eagerly parsed (high-frequency analysis/rendering needs),
  // Type-specific fields are lazily parsed as raw spans (secondary parsing on demand).

  // Read 3 doubles from List<Double> (Pos/Motion)
  [[nodiscard]] ParseResult<std::array<double, 3>>
    ReadVec3Double(nbt::ByteReader& reader);

  // Read 2 floats from List<Float> (Rotation: yaw, pitch)
  [[nodiscard]] ParseResult<std::array<float, 2>>
    ReadVec2Float(nbt::ByteReader& reader);

  // Parse single Entity Compound
  // raw_start: cursor position before entering Compound payload (for raw span slicing)
  [[nodiscard]] ParseResult<Entity> ParseEntityCompound(
    nbt::ByteReader& reader);

  // Parse Entities List
  [[nodiscard]] ParseResult<void> ParseEntities(
    nbt::ByteReader& reader, std::vector<Entity>& entities);

}  // namespace fschema::parser::litematic::detail

#endif  // FSCHEMA_PARSER_LITEMATIC_DETAIL_ENTITY_H_