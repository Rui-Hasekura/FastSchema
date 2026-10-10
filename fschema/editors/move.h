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

#ifndef FSCHEMA_EDITORS_MOVE_H_
#define FSCHEMA_EDITORS_MOVE_H_

#include <cstdint>
#include <limits>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"

namespace fschema::editors {

/// Clears the source selection (sets to air) and writes the original blocks
/// to their new positions shifted by (dx, dy, dz).
///
/// Out-of-bounds destinations are discarded (the block vanishes),
/// but the source is still cleared for those blocks.
///
/// To support overlapping moves, selected blocks are collected
/// before any writes.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Move(filters::BasicView<F> v,
                                     std::int32_t dx,
                                     std::int32_t dy,
                                     std::int32_t dz) {
  ir::Region& r = v.region();
  const auto& bounds = r.bounds;

  constexpr std::uint64_t kOOB = std::numeric_limits<std::uint64_t>::max();

  struct Entry {
    std::uint64_t src_linear;
    std::uint64_t dst_linear;
    std::uint16_t pal;
  };

  std::vector<Entry> entries;

  const std::uint64_t sx = bounds.size[0];
  const std::uint64_t sz = bounds.size[2];
  const std::uint64_t y_stride = sx * sz;

  v.for_each([&](filters::LocalPos p, std::uint16_t pal) {
    const std::int32_t dst_x = p.x + dx;
    const std::int32_t dst_y = p.y + dy;
    const std::int32_t dst_z = p.z + dz;

    Entry e;
    e.src_linear = static_cast<std::uint64_t>(p.y) * y_stride +
                   static_cast<std::uint64_t>(p.z) * sx +
                   static_cast<std::uint64_t>(p.x);
    e.pal = pal;
    if (dst_x >= 0 && dst_x < bounds.size[0] && dst_y >= 0 &&
        dst_y < bounds.size[1] && dst_z >= 0 && dst_z < bounds.size[2]) {
      e.dst_linear = static_cast<std::uint64_t>(dst_y) * y_stride +
                     static_cast<std::uint64_t>(dst_z) * sx +
                     static_cast<std::uint64_t>(dst_x);
    } else {
      e.dst_linear = kOOB;
    }
    entries.push_back(e);
  });

  const std::uint16_t air_idx = ResolveAir(r);
  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();

  for (const auto& e : entries) {
    data[e.src_linear] = air_idx;
  }

  for (const auto& e : entries) {
    if (e.dst_linear != kOOB) {
      data[e.dst_linear] = e.pal;
    }
  }
  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_MOVE_H_