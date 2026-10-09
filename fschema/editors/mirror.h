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

#ifndef FSCHEMA_EDITORS_MIRROR_H_
#define FSCHEMA_EDITORS_MIRROR_H_

#include <cstdint>
#include <utility>

#include "fschema/base/error.h"
#include "fschema/editors/extract.h"
#include "fschema/editors/internal/state_transform.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

// Mirror: Reflect the selected blocks along a given axis.
//
// The caller guarantees the structure does not contain blocks that
// become invalid when mirrored (e.g., cactus, flower pots).
template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Mirror(
    const filters::BasicView<F>& v,
    filters::Axis axis,
    memory::Arena& arena,
    StateTransformMode mode = StateTransformMode::kGeometricOnly) {
  // Step 1: Extract the selection into a pristine region.
  auto extract_res = Extract(v, arena);
  if (!extract_res) return std::unexpected(extract_res.error());
  ir::Region out = std::move(*extract_res);

  const std::int32_t sx = out.bounds.size[0];
  const std::int32_t sy = out.bounds.size[1];
  const std::int32_t sz = out.bounds.size[2];

  const std::int32_t axis_dim = (axis == filters::Axis::X)   ? sx
                                : (axis == filters::Axis::Y) ? sy
                                                             : sz;
  // If the region is flat or empty on the mirror axis, it is a no-op.
  if (axis_dim <= 1) return out;

  // Step 2: In-place swap to mirror the block indices.
  std::uint16_t* data = out.block_indices.data();

  const std::uint64_t y_stride = static_cast<std::uint64_t>(sx) * sz;

  if (axis == filters::Axis::X) {
    for (std::int32_t y = 0; y < sy; ++y) {
      for (std::int32_t z = 0; z < sz; ++z) {
        const std::uint64_t row_base =
            static_cast<std::uint64_t>(y) * y_stride +
            static_cast<std::uint64_t>(z) * sx;
        std::uint64_t i1 = row_base;
        std::uint64_t i2 = row_base + (sx - 1);
        for (std::int32_t x = 0; x < sx / 2; ++x) {
          std::swap(data[i1], data[i2]);
          ++i1;
          --i2;
        }
      }
    }
  } else if (axis == filters::Axis::Y) {
    for (std::int32_t z = 0; z < sz; ++z) {
      for (std::int32_t x = 0; x < sx; ++x) {
        const std::uint64_t col_base =
            static_cast<std::uint64_t>(z) * sx + static_cast<std::uint64_t>(x);
        std::uint64_t i1 = col_base;
        std::uint64_t i2 = col_base + (sy - 1) * y_stride;
        for (std::int32_t y = 0; y < sy / 2; ++y) {
          std::swap(data[i1], data[i2]);
          i1 += y_stride;
          i2 -= y_stride;
        }
      }
    }
  } else {  // Axis::Z
    for (std::int32_t y = 0; y < sy; ++y) {
      for (std::int32_t x = 0; x < sx; ++x) {
        const std::uint64_t row_base =
            static_cast<std::uint64_t>(y) * y_stride +
            static_cast<std::uint64_t>(x);
        std::uint64_t i1 = row_base;
        std::uint64_t i2 = row_base + (sz - 1) * sx;
        for (std::int32_t z = 0; z < sz / 2; ++z) {
          std::swap(data[i1], data[i2]);
          i1 += sx;
          i2 -= sx;
        }
      }
    }
  }

  if (mode == StateTransformMode::kTransformStates) {
    internal::TransformPalette(
        out, internal::AxisOpType::kMirror, axis, 1, arena);
  }

  return out;
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_MIRROR_H_