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

#ifndef FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_
#define FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_

#include <cstdint>
#include <limits>

#include "fschema/base/block_utils.h"
#include "fschema/base/error.h"
#include "fschema/editors/extract.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

/// Scans the region for non-air blocks, calculates the tightest bounding box
/// containing them, and extracts a new compact Region.
///
/// @return An empty region (size 0) if the input is entirely air.
[[nodiscard]] inline ParseResult<ir::Region> BoundingBoxTrim(
    ir::Region& r,
    memory::Arena& arena) {
  auto mat_res = ir::EnsureMaterialized(r, arena);
  if (!mat_res) return std::unexpected(mat_res.error());

  const std::int32_t sx = r.bounds.size[0];
  const std::int32_t sy = r.bounds.size[1];
  const std::int32_t sz = r.bounds.size[2];

  std::int32_t min_x = std::numeric_limits<std::int32_t>::max();
  std::int32_t min_y = std::numeric_limits<std::int32_t>::max();
  std::int32_t min_z = std::numeric_limits<std::int32_t>::max();
  std::int32_t max_x = std::numeric_limits<std::int32_t>::min();
  std::int32_t max_y = std::numeric_limits<std::int32_t>::min();
  std::int32_t max_z = std::numeric_limits<std::int32_t>::min();

  // Scan for non-air bounds
  bool any_non_air = false;
  const std::uint64_t sx = r.bounds.size[0];
  const std::uint64_t sz = r.bounds.size[2];
  const std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();

  std::uint64_t li = 0;
  for (std::int32_t y = 0; y < sy; ++y) {
    for (std::int32_t z = 0; z < sz; ++z) {
      for (std::int32_t x = 0; x < sx; ++x, ++li) {
        std::uint16_t pal = data[li];
        if (pal < r.palette.size() && !r.IsAir(pal)) {
          any_non_air = true;
          if (x < min_x) min_x = x;
          if (y < min_y) min_y = y;
          if (z < min_z) min_z = z;
          if (x > max_x) max_x = x;
          if (y > max_y) max_y = y;
          if (z > max_z) max_z = z;
        }
      }
    }
  }

  if (!any_non_air) {
    ir::Region empty;
    empty.name = "trimmed_empty";
    empty.bounds = {{0, 0, 0}, {0, 0, 0}};
    empty.position = {0, 0, 0};
    empty.size = {0, 0, 0};
    empty.is_materialized = true;
    return empty;
  }

  filters::BoxFilter box{min_x, min_y, min_z, max_x, max_y, max_z};
  auto view_res = filters::MakeView(r, box, arena);
  if (!view_res) return std::unexpected(view_res.error());

  return Extract(*view_res, arena);
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_