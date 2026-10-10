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

#ifndef FSCHEMA_EDITORS_COPY_H_
#define FSCHEMA_EDITORS_COPY_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"
#include "fschema/ir/materialize.h"

namespace fschema::editors {

/// Copies blocks from the view's source region to a target region
/// at position (target_pos + offset).
///
/// Behavior:
/// 1. Out-of-bounds destinations in the target are silently discarded.
/// 2. Palette entries from the source are remapped to the target palette by
///    name (properties are not carried over).
///
/// Note: Does not call `MarkEdited` on the source,
/// but calls `ResolveOrAppend` on the target,
/// which may modify the target's palette.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Copy(const filters::BasicView<F>& v,
                                     ir::Region& target,
                                     std::int32_t dx,
                                     std::int32_t dy,
                                     std::int32_t dz,
                                     memory::Arena& arena) {
  ir::Region& src = v.region();
  auto mat_res = ir::EnsureMaterialized(target, arena);
  if (!mat_res) return std::unexpected(mat_res.error());

  const auto& dst_bounds = target.bounds;

  // Build palette remap: src_palette_idx -> dst_palette_idx (by name).
  std::vector<std::uint16_t> remap(src.palette.size(), 0);
  absl::flat_hash_map<std::string_view, std::uint16_t> target_lookup;
  target_lookup.reserve(target.palette.size());
  for (std::size_t i = 0; i < target.palette.size(); ++i) {
    target_lookup.try_emplace(target.palette[i].name,
                              static_cast<std::uint16_t>(i));
  }

  for (std::size_t i = 0; i < src.palette.size(); ++i) {
    auto it = target_lookup.find(src.palette[i].name);
    if (it != target_lookup.end()) {
      remap[i] = it->second;
    } else {
      target.lazy_source.palette_pristine = false;
      std::uint16_t new_idx = AppendPaletteEntry(target, src.palette[i].name);
      target_lookup.try_emplace(src.palette[i].name, new_idx);
      remap[i] = new_idx;
    }
  }

  std::uint16_t* FSCHEMA_RESTRICT dst_data = target.block_indices.data();

  const std::uint64_t dst_sx = dst_bounds.size[0];
  const std::uint64_t dst_sz = dst_bounds.size[2];
  const std::uint64_t dst_y_stride = dst_sx * dst_sz;

  v.for_each([&](filters::LocalPos p, std::uint16_t pal) {
    const std::int32_t dst_x = p.x + dx;
    const std::int32_t dst_y = p.y + dy;
    const std::int32_t dst_z = p.z + dz;
    if (dst_x < 0 || dst_x >= dst_bounds.size[0]) return;
    if (dst_y < 0 || dst_y >= dst_bounds.size[1]) return;
    if (dst_z < 0 || dst_z >= dst_bounds.size[2]) return;
    const std::uint64_t dst_linear =
        static_cast<std::uint64_t>(dst_y) * dst_y_stride +
        static_cast<std::uint64_t>(dst_z) * dst_sx +
        static_cast<std::uint64_t>(dst_x);
    dst_data[dst_linear] = remap[pal];
  });

  MarkEdited(target);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_COPY_H_