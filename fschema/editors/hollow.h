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

#ifndef FSCHEMA_EDITORS_HOLLOW_H_
#define FSCHEMA_EDITORS_HOLLOW_H_

#include <cstdint>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

/// Removes all internal blocks, leaving only a shell of thickness 1.
///
/// This is achieved by selecting all blocks that are NOT exposed to
/// air AND NOT air themselves, then setting them to air.
///
/// Uses a pre-calculated `exposed[]` mask to avoid repeated 6-neighbor
/// checks and cascading in-place mutations during the scan.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Hollow(filters::BasicView<F> v,
                                       memory::Arena& arena) {
  ir::Region& r = v.region();
  auto mat_res = ir::EnsureMaterialized(r, arena);
  if (!mat_res) return std::unexpected(mat_res.error());

  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  if (vol == 0) return {};

  // Precompute an `exposed` mask for the entire region.
  // A block is exposed if it is on the boundary or has at least one air
  // neighbor. We allocate from the arena to avoid large std::vector heap
  // allocations.
  std::uint8_t* exposed = arena.AllocateArray<std::uint8_t>(vol);

  const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;
  const std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();

  std::uint64_t li = 0;
  for (std::int32_t y = 0; y < sy; ++y) {
    for (std::int32_t z = 0; z < sz; ++z) {
      for (std::int32_t x = 0; x < sx; ++x, ++li) {
        bool is_exp = false;
        // Check 6 neighbors with short-circuit evaluation. The boundary checks
        // (x == 0, etc.) guard the r.IsAir() calls to prevent OOB reads.
        if (x == 0 || r.IsAir(data[li - 1]))
          is_exp = true;
        else if (x == sx - 1 || r.IsAir(data[li + 1]))
          is_exp = true;
        else if (z == 0 || r.IsAir(data[li - sx]))
          is_exp = true;
        else if (z == sz - 1 || r.IsAir(data[li + sx]))
          is_exp = true;
        else if (y == 0 || r.IsAir(data[li - y_stride]))
          is_exp = true;
        else if (y == sy - 1 || r.IsAir(data[li + y_stride]))
          is_exp = true;

        exposed[li] = is_exp ? 1 : 0;
      }
    }
  }

  const std::uint16_t air_idx = ResolveAir(r);
  std::uint16_t* FSCHEMA_RESTRICT data_mut = r.block_indices.data();

  // Second pass: iterate over the filtered view and apply deletions.
  // Because `exposed[]` is pre-calculated based on the original state,
  // modifying `data_mut` here does not cascade and affect other blocks'
  // exposure.
  v.for_each_linear([&](std::uint64_t linear_idx, std::uint16_t pal) {
    if (pal < r.palette.size() && !exposed[linear_idx] && !r.IsAir(pal)) {
      data_mut[linear_idx] = air_idx;
    }
  });

  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_HOLLOW_H_