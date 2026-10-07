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

#ifndef FSCHEMA_FILTERS_SPHERE_FILTER_H_
#define FSCHEMA_FILTERS_SPHERE_FILTER_H_

#include <cstdint>

#include "fschema/filters/pos.h"

namespace fschema::filters {

/// Selects blocks within a sphere.
///
/// r2 is the squared radius to avoid sqrt in the inner loop.
struct SphereFilter {
  std::int32_t cx, cy, cz;
  std::int64_t r2;

  [[nodiscard]] constexpr bool operator()(LocalPos p, std::uint16_t /*idx=*/)
      const noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(p.x) - cx;
    const std::int64_t dy = static_cast<std::int64_t>(p.y) - cy;
    const std::int64_t dz = static_cast<std::int64_t>(p.z) - cz;
    return dx * dx + dy * dy + dz * dz <= r2;
  }
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_SPHERE_FILTER_H_