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

#ifndef FSCHEMA_EDITORS_EXTRACT_H_
#define FSCHEMA_EDITORS_EXTRACT_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

/// Scans the view and computes the bounding box of the selected blocks.
///
/// @return The bounding box in *world space* (origin offset applied),
/// or `std::nullopt` if the selection is empty.
template <filters::Filter F>
[[nodiscard]] std::optional<ir::BoundingBox> ComputeSelectionBounds(
    const filters::BasicView<F>& v) {
  const ir::Region& r = v.region();
  std::int32_t min_x = std::numeric_limits<std::int32_t>::max();
  std::int32_t min_y = std::numeric_limits<std::int32_t>::max();
  std::int32_t min_z = std::numeric_limits<std::int32_t>::max();
  std::int32_t max_x = std::numeric_limits<std::int32_t>::min();
  std::int32_t max_y = std::numeric_limits<std::int32_t>::min();
  std::int32_t max_z = std::numeric_limits<std::int32_t>::min();

  bool any = false;
  v.for_each([&](filters::LocalPos p, std::uint16_t) {
    any = true;
    if (p.x < min_x) min_x = p.x;
    if (p.y < min_y) min_y = p.y;
    if (p.z < min_z) min_z = p.z;
    if (p.x > max_x) max_x = p.x;
    if (p.y > max_y) max_y = p.y;
    if (p.z > max_z) max_z = p.z;
  });

  if (!any) return std::nullopt;

  ir::BoundingBox bb;
  // Convert local bounds back to world-space bounds
  bb.origin[0] = r.bounds.origin[0] + min_x;
  bb.origin[1] = r.bounds.origin[1] + min_y;
  bb.origin[2] = r.bounds.origin[2] + min_z;
  bb.size[0] = max_x - min_x + 1;
  bb.size[1] = max_y - min_y + 1;
  bb.size[2] = max_z - min_z + 1;
  return bb;
}

/// Deep-copies the selected blocks into a new, compact `Region`.
///
/// @return The extracted region.
template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Extract(const filters::BasicView<F>& v,
                                              memory::Arena& arena) {
  const ir::Region& src = v.region();

  auto bounds_opt = ComputeSelectionBounds(v);
  if (!bounds_opt) {
    ir::Region out;
    out.name = "extract";
    out.bounds = {{0, 0, 0}, {0, 0, 0}};
    out.position = {0, 0, 0};
    out.size = {0, 0, 0};
    out.is_materialized = true;
    return out;
  }
  const ir::BoundingBox& bb = *bounds_opt;

  // Local offset from selection origin (relative to source region).
  const std::int32_t off_x = bb.origin[0] - src.bounds.origin[0];
  const std::int32_t off_y = bb.origin[1] - src.bounds.origin[1];
  const std::int32_t off_z = bb.origin[2] - src.bounds.origin[2];

  // First pass: mark used palette entries.
  std::vector<std::uint8_t> used(src.palette.size(), 0);
  v.for_each([&](filters::LocalPos, std::uint16_t pal) {
    if (pal < used.size()) used[pal] = 1;
  });

  // Build new palette (subset, original order) and remap.
  std::vector<std::uint16_t> remap(src.palette.size(), 0);
  ir::Region out;
  out.name = "extract";
  out.bounds = bb;
  out.position = {bb.origin[0], bb.origin[1], bb.origin[2]};
  out.size = {bb.size[0], bb.size[1], bb.size[2]};
  out.palette.reserve(src.palette.size());
  for (std::size_t i = 0; i < src.palette.size(); ++i) {
    if (used[i]) {
      remap[i] = static_cast<std::uint16_t>(out.palette.size());
      out.palette.push_back(src.palette[i]);
    }
  }

  const std::uint64_t vol = static_cast<std::uint64_t>(bb.size[0]) *
                            static_cast<std::uint64_t>(bb.size[1]) *
                            static_cast<std::uint64_t>(bb.size[2]);
  out.block_indices.resize_uninitialized(static_cast<std::size_t>(vol), &arena);

  // Fill with the first palette entry (may or may not be air; the encoder
  // normalizes at serialization time).
  if (vol > 0 && !out.palette.empty()) {
    std::fill_n(out.block_indices.data(), vol, static_cast<std::uint16_t>(0));
  }

  const std::int32_t dst_sx = bb.size[0];
  const std::int32_t dst_sz = bb.size[2];
  std::uint16_t* FSCHEMA_RESTRICT dst_data = out.block_indices.data();
  v.for_each([&](filters::LocalPos p, std::uint16_t pal) {
    const std::int32_t lx = p.x - off_x;
    const std::int32_t ly = p.y - off_y;
    const std::int32_t lz = p.z - off_z;
    const std::uint64_t dst_linear =
        static_cast<std::uint64_t>(ly) * dst_sx * dst_sz +
        static_cast<std::uint64_t>(lz) * dst_sx +
        static_cast<std::uint64_t>(lx);
    dst_data[dst_linear] = remap[pal];
  });

  out.is_materialized = true;
  return out;
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_EXTRACT_H_