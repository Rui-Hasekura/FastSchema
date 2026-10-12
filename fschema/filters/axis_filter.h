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

#ifndef FSCHEMA_FILTERS_AXIS_FILTER_H_
#define FSCHEMA_FILTERS_AXIS_FILTER_H_

#include <cstdint>

#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::filters {

/// Selects blocks whose local coordinate on a specific `axis` satisfies
/// the condition `axis_value op threshold`.
struct AxisFilter {
  Axis axis;
  Cmp op;
  std::int32_t threshold;

  [[nodiscard]] constexpr bool operator()(LocalPos p, std::uint16_t /*idx=*/)
      const noexcept {
    const std::int32_t v = (axis == Axis::kX)   ? p.x
                           : (axis == Axis::kY) ? p.y
                                               : p.z;
    switch (op) {
      case Cmp::kLt:
        return v < threshold;
      case Cmp::kLe:
        return v <= threshold;
      case Cmp::kGe:
        return v >= threshold;
      case Cmp::kGt:
        return v > threshold;
      case Cmp::kEq:
        return v == threshold;
      case Cmp::kNe:
        return v != threshold;
    }
    return false;
  }
};

/// Factory function to build an `AxisFilter` from a world-space threshold.
/// Subtracts the region's origin along the given axis to
/// convert the world threshold into a local threshold.
[[nodiscard]] inline AxisFilter WorldAxis(
    const ir::Region& r,
    Axis axis,
    Cmp op,
    std::int32_t world_threshold) noexcept {
  const std::int32_t local =
      world_threshold - r.bounds.origin[static_cast<int>(axis)];
  return AxisFilter{axis, op, local};
}

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_AXIS_FILTER_H_