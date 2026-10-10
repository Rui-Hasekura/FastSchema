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

#ifndef FSCHEMA_FILTERS_SURFACE_FILTER_H_
#define FSCHEMA_FILTERS_SURFACE_FILTER_H_

#include <cstdint>
#include <vector>

#include "fschema/filters/internal/neighbors.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::filters {

/// Selects blocks that are exposed to air on at least one of their 6 faces.
///
/// Stateful filter checking block exposure against a Region.
/// Treats out-of-bounds neighbors as air.
///
/// Contract:
/// 1. `r.is_materialized == true` must hold before construction.
/// 2. Air blocks are never selected (they are not "surface").
class SurfaceFilter {
 public:
  explicit SurfaceFilter(const ir::Region& r) noexcept : region_(&r) {}

    [[nodiscard]] bool operator()(LocalPos p, std::uint16_t idx) const noexcept {
    if (idx >= region_->palette.size()) return false;
    if (region_->IsAir(idx)) return false;
    return filters::internal::IsExposedAt(*region_, p.x, p.y, p.z);
  }

 private:
  const ir::Region* region_;
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_SURFACE_FILTER_H_