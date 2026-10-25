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

#include "fschema/geometry/internal/chunk_dispatch.h"

#include <cstdint>
#include <vector>

#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::geometry::internal {

std::vector<filters::LocalPos> ComputeChunkOrigins(
    const ir::BoundingBox& bounds,
    std::uint32_t chunk_edge) {
  std::vector<filters::LocalPos> out;
  if (chunk_edge == 0) return out;

  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::int32_t ce = static_cast<std::int32_t>(chunk_edge);

  const std::int32_t nx = (sx + ce - 1) / ce;
  const std::int32_t ny = (sy + ce - 1) / ce;
  const std::int32_t nz = (sz + ce - 1) / ce;

  out.reserve(static_cast<std::size_t>(nx) * ny * nz);

  // YZX order matches the block_indices memory layout.
  for (std::int32_t cy = 0; cy < ny; ++cy) {
    for (std::int32_t cz = 0; cz < nz; ++cz) {
      for (std::int32_t cx = 0; cx < nx; ++cx) {
        out.push_back(filters::LocalPos{cx * ce, cy * ce, cz * ce});
      }
    }
  }
  return out;
}

}  // namespace fschema::geometry::internal
