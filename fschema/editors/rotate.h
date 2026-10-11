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

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

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
template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Rotate(
    const filters::BasicView<F>& v,
    filters::Axis axis,
    int steps,
    memory::Arena& arena,
    StateTransformMode mode = StateTransformMode::kGeometricOnly) {
  steps = ((steps % 4) + 4) % 4;
  if (steps == 0) {
    return Extract(v, arena);
  }

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

  const std::uint64_t src_sx = sx;
  const std::uint64_t src_y_stride = static_cast<std::uint64_t>(sx) * sz;

  memory::UnInitBuffer<std::uint16_t> new_data(static_cast<std::size_t>(vol),
                                               &arena);
  const std::uint16_t* src = out.block_indices.data();
  std::uint16_t* dst = new_data.data();

  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks

  auto process_y = [&](std::int32_t dst_y) {
    const std::uint64_t dst_y_base =
        static_cast<std::uint64_t>(dst_y) * new_y_stride;
    for (std::int32_t dst_z = 0; dst_z < new_sz; ++dst_z) {
      const std::uint64_t dst_z_base =
          dst_y_base + static_cast<std::uint64_t>(dst_z) * new_sx;
      for (std::int32_t dst_x = 0; dst_x < new_sx; ++dst_x) {
        std::uint64_t src_idx = 0;

        // Reverse mapping: find the source index for the current dst block.
        if (axis == filters::Axis::Y) {
          if (steps == 1) {
            src_idx = dst_y * src_y_stride + dst_x * src_sx + (sx - 1 - dst_z);
          } else if (steps == 2) {
            src_idx = dst_y * src_y_stride + (sz - 1 - dst_z) * src_sx +
                      (sx - 1 - dst_x);
          } else {  // steps == 3
            src_idx = dst_y * src_y_stride + (sz - 1 - dst_x) * src_sx + dst_z;
          }
        } else if (axis == filters::Axis::X) {
          if (steps == 1) {
            src_idx = (sy - 1 - dst_z) * src_y_stride + dst_y * src_sx + dst_x;
          } else if (steps == 2) {
            src_idx = (sy - 1 - dst_y) * src_y_stride +
                      (sz - 1 - dst_z) * src_sx + dst_x;
          } else {  // steps == 3
            src_idx = dst_z * src_y_stride + (sz - 1 - dst_y) * src_sx + dst_x;
          }
        } else {  // Axis::Z
          if (steps == 1) {
            src_idx = dst_x * src_y_stride + dst_z * src_sx + (sx - 1 - dst_y);
          } else if (steps == 2) {
            src_idx = (sy - 1 - dst_y) * src_y_stride + dst_z * src_sx +
                      (sx - 1 - dst_x);
          } else {  // steps == 3
            src_idx = (sy - 1 - dst_x) * src_y_stride + dst_z * src_sx + dst_y;
          }
        }
        dst[dst_z_base + dst_x] = src[src_idx];
      }
    }
  };

  if (vol < kParallelThreshold) {
    for (std::int32_t y = 0; y < new_bounds.size[1]; ++y) {
      process_y(y);
    }
  } else {
    tbb::parallel_for(
        tbb::blocked_range<std::int32_t>(0, new_bounds.size[1]),
        [&](const tbb::blocked_range<std::int32_t>& range) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            process_y(y);
          }
        },
        tbb::static_partitioner{});
  }

  out.bounds = new_bounds;
  out.position = {
      new_bounds.origin[0], new_bounds.origin[1], new_bounds.origin[2]};
  out.size = {new_bounds.size[0], new_bounds.size[1], new_bounds.size[2]};
  out.block_indices = std::move(new_data);

  if (mode == StateTransformMode::kTransformStates && steps != 0) {
    internal::TransformPalette(
        out, internal::AxisOpType::kRotate, axis, steps, arena);
  }

  return out;
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_ROTATE_H_