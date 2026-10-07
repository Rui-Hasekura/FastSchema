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

#ifndef FSCHEMA_FILTERS_BOX_FILTER_H_
#define FSCHEMA_FILTERS_BOX_FILTER_H_

#include <cstdint>

#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::filters {

/// Selects blocks within an axis-aligned 3D bounding box.
///
/// Bounds are inclusive. Coordinates are region-local.
struct BoxFilter {
  std::int32_t min_x, min_y, min_z;
  std::int32_t max_x, max_y, max_z;  // inclusive bounds

  [[nodiscard]] constexpr bool operator()(LocalPos p, std::uint16_t /*idx=*/)
      const noexcept {
    return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y &&
           p.z >= min_z && p.z <= max_z;
  }
};

/// Factory function to create a `BoxFilter` from world-space coordinates.
/// Subtracts the region's origin to convert world coordinates to local
/// coordinates.
[[nodiscard]] inline BoxFilter WorldBox(const ir::Region& r,
                                        std::int32_t wmin_x,
                                        std::int32_t wmin_y,
                                        std::int32_t wmin_z,
                                        std::int32_t wmax_x,
                                        std::int32_t wmax_y,
                                        std::int32_t wmax_z) noexcept {
  return BoxFilter{wmin_x - r.bounds.origin[0],
                   wmin_y - r.bounds.origin[1],
                   wmin_z - r.bounds.origin[2],
                   wmax_x - r.bounds.origin[0],
                   wmax_y - r.bounds.origin[1],
                   wmax_z - r.bounds.origin[2]};
}

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_BOX_FILTER_H_