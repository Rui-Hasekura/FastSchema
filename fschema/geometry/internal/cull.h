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

#ifndef FSCHEMA_GEOMETRY_INTERNAL_CULL_H_
#define FSCHEMA_GEOMETRY_INTERNAL_CULL_H_

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fschema/ir/types.h"

namespace fschema::geometry::internal {

// Per-block cull/AO classification, one byte per block:
//   0 = air, 1 = opaque, 2+ = transparent group id. Blocks that share a
//        transparent group cull against each other (water against water,
//        glass against glass); transparent blocks never cull other
//        blocks' faces. Built once per region from `block_indices` and
//        per-palette-entry groups. Reads are thread-safe; the table must
//        outlive the workers reading it.
struct BlockGroupTable {
  std::vector<std::uint8_t> bytes;  // size == region.block_indices.size()

  void BuildFrom(const ir::Region& region,
                 std::span<const std::uint8_t> palette_groups);

  // Unchecked lookups by linear index.
  [[nodiscard]] bool IsAir(std::size_t linear_idx) const noexcept {
    return bytes[linear_idx] == 0;
  }
  [[nodiscard]] bool IsOpaque(std::size_t linear_idx) const noexcept {
    return bytes[linear_idx] == 1;
  }
};

// Groups >= kNonCullingGroupBase are transparent groups that hide
// nothing, not even same-group neighbors: the leaves family. Vanilla
// fancy-graphics leaves neither occlude neighbors (canOcclude false)
// nor cull faces between adjacent leaf blocks (LeavesBlock overrides
// no skipRendering), unlike glass (glass-vs-glass culls). Ids 2..127
// stay reserved for the self-culling transparent groups.
inline constexpr std::uint8_t kNonCullingGroupBase = 128;

// Whether a face of a block in group `cur_group` is hidden by a neighbor
// in group `nb_group`: an opaque neighbor always hides it, and a
// self-culling transparent neighbor hides it only when both blocks
// belong to the same transparent group. Non-culling groups (>=
// kNonCullingGroupBase) and air (0) hide nothing; out-of-bounds
// neighbors read as air.
[[nodiscard]] constexpr bool NeighborCulls(std::uint8_t nb_group,
                                           std::uint8_t cur_group) noexcept {
  if (nb_group >= kNonCullingGroupBase) return false;
  return nb_group == 1 || (nb_group >= 2 && nb_group == cur_group);
}

// Highway-SIMD batch neighbor lookup for the +X axis. For the `count`
// blocks at linear indices [base, base+count) (typically one X row),
// copies their +X neighbor's group byte into out_groups:
//   out_groups[i] != 0  iff the +X neighbor of block base+i is inside
//                        the same X row (`row_width` = region size along
//                        X); the byte is then that neighbor's group
//   out_groups[i] == 0  otherwise -- an air, out-of-bounds, or cross-row
//                        neighbor leaves the face visible
//
// Feed the result to NeighborCulls with the current block's group.
// out_groups must have room for `count` bytes.
void ComputePosXNeighborGroupBatch(const BlockGroupTable& table,
                                   std::size_t base,
                                   std::size_t count,
                                   std::uint32_t row_width,
                                   std::uint8_t* out_groups);

// Same, for the -X neighbors (base+i-1).
void ComputeNegXNeighborGroupBatch(const BlockGroupTable& table,
                                   std::size_t base,
                                   std::size_t count,
                                   std::uint32_t row_width,
                                   std::uint8_t* out_groups);

}  // namespace fschema::geometry::internal

#endif  // FSCHEMA_GEOMETRY_INTERNAL_CULL_H_
