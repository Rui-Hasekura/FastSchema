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

#ifndef FSCHEMA_INSPECTORS_LIST_TILE_ENTITY_H_
#define FSCHEMA_INSPECTORS_LIST_TILE_ENTITY_H_

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

#include "fschema/ir/types.h"
#include "fschema/third_party/pdqsort/pdqsort.h"

namespace fschema::inspectors {

struct TileEntityEntry {
  std::string_view id;
  std::array<std::int32_t, 3> block_position;  // world-space
};

/// Enumerates block entities (tile entities) in a region.
///
/// Converts the internal `ir::BlockEntity` into a `TileEntityEntry`.
/// Note: `block_position` is in world space.
///
/// @param use_sort If true, sorts entries by `id` ascending using `pdqsort`.
[[nodiscard]] inline std::vector<TileEntityEntry> ListTileEntity(
    const ir::Region& r,
    bool use_sort = false) {
  std::vector<TileEntityEntry> entries;
  entries.reserve(r.block_entities.size());
  for (const auto& be : r.block_entities) {
    entries.push_back(TileEntityEntry{be.id, be.block_position});
  }

  if (use_sort) {
    pdqsort(entries.begin(),
            entries.end(),
            [](const TileEntityEntry& a, const TileEntityEntry& b) {
              return a.id < b.id;
            });
  }
  return entries;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_LIST_TILE_ENTITY_H_