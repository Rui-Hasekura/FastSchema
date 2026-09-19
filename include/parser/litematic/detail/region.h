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

#ifndef FSCHEMA_PARSER_LITEMATIC_DETAIL_REGION_H_
#define FSCHEMA_PARSER_LITEMATIC_DETAIL_REGION_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <memory>
#include <string>
#include <vector>

#include "parser/error.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"

namespace fschema::parser::litematic::detail {

  // Regions Compound: every region is a Compound entry
  //   <region_name>: Compound {
  //     Position: Compound { x,y,z: Int }
  //     Size: Compound { x,y,z: Int }
  //     BlockStatePalette: List<Compound>
  //     BlockStates: LongArray
  //     TileEntities: List<Compound>
  //     Entities: List<Compound>
  //     PendingBlockTicks / PendingFluidTicks (v6+)
  //     PendingBlockEntities / PendingEntities (v6+)
  //   }
  // Field order is not fixed (BlockStates can appear before Size/Palette),
  // so BlockStates is zero-copied as a span, and unpacking is delayed until
  // the end of the region.

  [[nodiscard]] ParseResult<std::array<std::int32_t, 3>>
    ParseVec3Int(nbt::ByteReader& reader);

  [[nodiscard]] ParseResult<Region> ParseRegion(
    nbt::ByteReader& reader, std::string region_name,
    std::unique_ptr<std::vector<std::byte>>& owner);

  [[nodiscard]] ParseResult<void> ParseRegions(
    nbt::ByteReader& reader, Litematic& out);

} // namespace fschema::parser::litematic::detail

#endif // FSCHEMA_PARSER_LITEMATIC_DETAIL_REGION_H_