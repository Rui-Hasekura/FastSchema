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

#ifndef FSCHEMA_EDITORS_ROTATE_H_
#define FSCHEMA_EDITORS_ROTATE_H_

#include <cstdint>
#include <utility>

#include "fschema/base/error.h"
#include "fschema/editors/extract.h"
#include "fschema/editors/internal/state_transform.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace fschema::editors {

// Rotate: rotate the selected blocks by 90-degree increments around an axis.
//
// The caller guarantees the structure does not contain blocks that
// become invalid when rotated (e.g., cactus, flower pots).
//
// steps: Number of 90-degree clockwise rotations (1, 2, or 3).
template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Rotate(
    const filters::BasicView<F>& v,
    filters::Axis axis,
    int steps,
    memory::Arena& arena,
    StateTransformMode mode = StateTransformMode::kGeometricOnly) {
  // Normalize steps to the range [0, 3]
  steps = ((steps % 4) + 4) % 4;
  if (steps == 0) {
    return Extract(v, arena);
  }

  // Step 1: Extract the selection into a pristine region.
  auto extract_res = Extract(v, arena);
  if (!extract_res) return std::unexpected(extract_res.error());
  ir::Region out = std::move(*extract_res);

  const std::int32_t sx = out.bounds.size[0];
  const std::int32_t sy = out.bounds.size[1];
  const std::int32_t sz = out.bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) *
                            static_cast<std::uint64_t>(sy) *
                            static_cast<std::uint64_t>(sz);

  if (vol == 0) return out;

  // Compute new bounds based on axis and steps
  ir::BoundingBox new_bounds = out.bounds;
  if (steps == 1 || steps == 3) {
    if (axis == filters::Axis::Y) {
      new_bounds.size[0] = sz;
      new_bounds.size[2] = sx;
    } else if (axis == filters::Axis::X) {
      new_bounds.size[1] = sz;
      new_bounds.size[2] = sy;
    } else {  // Axis::Z
      new_bounds.size[0] = sy;
      new_bounds.size[1] = sx;
    }
  }

  const std::uint64_t new_sx = new_bounds.size[0];
  const std::uint64_t new_sz = new_bounds.size[2];
  const std::uint64_t new_y_stride = new_sx * new_sz;
  const std::uint64_t new_z_stride = new_sx;

  // Allocate new buffer for the rotated state.
  // UnInitBuffer does not value-initialize, which is fine since we overwrite
  // it fully.
  memory::UnInitBuffer<std::uint16_t> new_data(static_cast<std::size_t>(vol),
                                               &arena);
  const std::uint16_t* src = out.block_indices.data();
  std::uint16_t* dst = new_data.data();

  // Iterate through the source in YZX order to read `src` sequentially.
  // Then compute the destination coordinates and linear index.
  std::uint64_t src_idx = 0;
  for (std::int32_t y = 0; y < sy; ++y) {
    for (std::int32_t z = 0; z < sz; ++z) {
      for (std::int32_t x = 0; x < sx; ++x, ++src_idx) {
        std::int32_t new_x = x;
        std::int32_t new_y = y;
        std::int32_t new_z = z;

        if (axis == filters::Axis::Y) {
          if (steps == 1) {
            // 90 deg CW: (x, y, z) -> (z, y, sx - 1 - x)
            new_x = z;
            new_z = sx - 1 - x;
          } else if (steps == 2) {
            // 180 deg: (x, y, z) -> (sx - 1 - x, y, sz - 1 - z)
            new_x = sx - 1 - x;
            new_z = sz - 1 - z;
          } else if (steps == 3) {
            // 270 deg CW: (x, y, z) -> (sz - 1 - z, y, x)
            new_x = sz - 1 - z;
            new_z = x;
          }
        } else if (axis == filters::Axis::X) {
          if (steps == 1) {
            // 90 deg CW: (x, y, z) -> (x, z, sy - 1 - y)
            new_y = z;
            new_z = sy - 1 - y;
          } else if (steps == 2) {
            // 180 deg: (x, y, z) -> (x, sy - 1 - y, sz - 1 - z)
            new_y = sy - 1 - y;
            new_z = sz - 1 - z;
          } else if (steps == 3) {
            // 270 deg CW: (x, y, z) -> (x, sz - 1 - z, y)
            new_y = sz - 1 - z;
            new_z = y;
          }
        } else {  // Axis::Z
          if (steps == 1) {
            // 90 deg CW: (x, y, z) -> (y, sx - 1 - x, z)
            new_x = y;
            new_y = sx - 1 - x;
          } else if (steps == 2) {
            // 180 deg: (x, y, z) -> (sx - 1 - x, sy - 1 - y, z)
            new_x = sx - 1 - x;
            new_y = sy - 1 - y;
          } else if (steps == 3) {
            // 270 deg CW: (x, y, z) -> (sy - 1 - y, x, z)
            new_x = sy - 1 - y;
            new_y = x;
          }
        }

        const std::uint64_t dst_idx =
            static_cast<std::uint64_t>(new_y) * new_y_stride +
            static_cast<std::uint64_t>(new_z) * new_z_stride +
            static_cast<std::uint64_t>(new_x);
        dst[dst_idx] = src[src_idx];
      }
    }
  }

  out.bounds = new_bounds;
  out.position = {
      new_bounds.origin[0],
      new_bounds.origin[1],
      new_bounds.origin[2]};
  out.size = {new_bounds.size[0], new_bounds.size[1], new_bounds.size[2]};
  out.block_indices = std::move(new_data);

  if (mode == StateTransformMode::kTransformStates && steps != 0) {
    internal::TransformPalette(
        out,
        internal::AxisOpType::kRotate,
        axis,
        steps,
        arena);
  }

  return out;
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_ROTATE_H_