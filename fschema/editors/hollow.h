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
#include <vector>

#include "fschema/base/error.h"
#include "fschema/editors/fill.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/block_name_filter.h"
#include "fschema/filters/composite_filter.h"
#include "fschema/filters/surface_filter.h"
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
/// Collects linear indices first, then applies deletions in
/// a second pass to avoid cascading in-place mutations.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Hollow(filters::BasicView<F> v,
                                       memory::Arena& arena) {
  ir::Region& r = v.region();
  auto mat_res = ir::EnsureMaterialized(r, arena);
  if (!mat_res) return std::unexpected(mat_res.error());

  filters::SurfaceFilter surface(r);
  filters::IsAirFilter is_air(r);

  // Select internal solid blocks: !surface && !air
  // Uses strongly-constrained operator overloads from composite_filter.h
  auto internal_filter = !surface && !is_air;

  auto internal_view_res = filters::MakeView(r, internal_filter, arena);
  if (!internal_view_res) return std::unexpected(internal_view_res.error());
  const auto& internal_view = *internal_view_res;

  std::vector<std::uint64_t> to_delete;
  internal_view.for_each_linear([&](std::uint64_t linear_idx, std::uint16_t) {
    to_delete.push_back(linear_idx);
  });

  if (to_delete.empty()) return {};

  const std::uint16_t air_idx = ResolveAir(r);
  std::uint16_t* data = r.block_indices.data();
  for (std::uint64_t idx : to_delete) {
    data[idx] = air_idx;
  }

  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_HOLLOW_H_