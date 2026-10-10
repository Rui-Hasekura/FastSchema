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

#ifndef FSCHEMA_EDITORS_OVERLAY_H_
#define FSCHEMA_EDITORS_OVERLAY_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

/// Places a specific block on top of the selected surfaces.
/// A surface is defined as a non-air block whose Y+1 neighbor is
/// air or out of bounds.
///
/// Target positions are collected before writing to
/// avoid cascading changes during the scan.
template <filters::Filter F>
[[nodiscard]] ParseResult<std::size_t> Overlay(filters::BasicView<F> v,
                                               std::string_view block_name,
                                               memory::Arena& arena) {
  ir::Region& r = v.region();
  auto mat_res = ir::EnsureMaterialized(r, arena);
  if (!mat_res) return std::unexpected(mat_res.error());

  const std::uint64_t target_idx = ResolveOrAppend(r, block_name);
  const auto& bounds = r.bounds;

  const std::uint64_t sx = bounds.size[0];
  const std::uint64_t sz = bounds.size[2];
  const std::uint64_t y_stride = sx * sz;

  std::vector<std::uint64_t> targets;
  v.for_each([&](filters::LocalPos p, std::uint16_t pal) {
    if (pal >= r.palette.size() || r.IsAir(pal)) {
      return;
    }

    const std::int32_t ty = p.y + 1;
    if (ty >= bounds.size[1]) {
      return;
    }

    const std::uint64_t top_linear = static_cast<std::uint64_t>(ty) * y_stride +
                                     static_cast<std::uint64_t>(p.z) * sx +
                                     static_cast<std::uint64_t>(p.x);
    const std::uint16_t top_pal = r.block_indices[top_linear];

    if (top_pal >= r.palette.size() || r.IsAir(top_pal)) {
      targets.push_back(top_linear);
    }
  });

  if (targets.empty()) {
    return std::size_t{0};
  }

  std::uint16_t* data = r.block_indices.data();
  for (std::uint64_t idx : targets) {
    data[idx] = target_idx;
  }

  MarkEdited(r);
  return targets.size();
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_OVERLAY_H_