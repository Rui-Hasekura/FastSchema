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

#ifndef FSCHEMA_INSPECTORS_LIST_ENTITY_H_
#define FSCHEMA_INSPECTORS_LIST_ENTITY_H_

#include <array>
#include <string_view>
#include <vector>

#include "fschema/ir/types.h"
#include "fschema/third_party/pdqsort/pdqsort.h"

namespace fschema::inspectors {

struct EntityEntry {
  std::string_view id;
  std::array<double, 3> position;  // world-space
  std::array<double, 3> motion;
  std::array<float, 2> rotation;  // yaw, pitch
};

/// Enumerates entities in a region.
///
/// Converts the internal `ir::Entity` into an `EntityEntry`.
///
/// @param use_sort If true, sorts entries by `id` ascending using `pdqsort`.
///                 This groups same-type entities together.
[[nodiscard]] inline std::vector<EntityEntry> ListEntity(
    const ir::Region& r,
    bool use_sort = false) {
  std::vector<EntityEntry> entries;
  entries.reserve(r.entities.size());
  for (const auto& ent : r.entities) {
    entries.push_back(
        EntityEntry{ent.id, ent.position, ent.motion, ent.rotation});
  }

  if (use_sort) {
    pdqsort(
        entries.begin(),
        entries.end(),
        [](const EntityEntry& a, const EntityEntry& b) { return a.id < b.id; });
  }
  return entries;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_LIST_ENTITY_H_