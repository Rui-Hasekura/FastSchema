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

#ifndef FSCHEMA_EDITORS_REPLACE_H_
#define FSCHEMA_EDITORS_REPLACE_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"

namespace fschema::editors {

/// Replaces all blocks whose palette name matches `from` with `to`.
///
/// Builds a boolean LUT of matching source palette indices during setup to
/// avoid string comparisons entirely.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> ReplaceWith(filters::BasicView<F> v,
                                            std::string_view from,
                                            std::string_view to) {
  ir::Region& r = v.region();

  // Build a LUT of source palette indices matching `from`.
  std::vector<std::uint8_t> is_from(r.palette.size(), 0);
  bool any_from = false;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (r.palette[i].name == from) {
      is_from[i] = 1;
      any_from = true;
    }
  }
  if (!any_from) return {};

  const std::uint16_t to_idx = ResolveOrAppend(r, to);

  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  v.for_each_linear([&](std::uint64_t linear_idx, std::uint16_t pal) {
    if (pal < is_from.size() && is_from[pal]) {
      data[linear_idx] = to_idx;
    }
  });

  MarkEdited(r);
  return {};
}

/// Generic replacement via a mapper callable.
///
/// @param mapper A callable taking `std::uint16_t` (old palette idx) and
///               returning `std::uint16_t` (new palette idx). Return the same
///               value to skip.
///
/// Use this when matching is more complex than name equality
/// (e.g., matching by properties,
///                 by a regex on names,
///              or by palette index ranges).
template <filters::Filter F, class Mapper>
  requires std::regular_invocable<const Mapper&, std::uint16_t> &&
           std::convertible_to<
               std::invoke_result_t<const Mapper&, std::uint16_t>,
               std::uint16_t>
[[nodiscard]] ParseResult<void> Replace(filters::BasicView<F> v,
                                        Mapper mapper) {
  ir::Region& r = v.region();
  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  v.for_each_linear([&](std::uint64_t linear_idx, std::uint16_t pal) {
    const std::uint16_t new_pal = mapper(pal);
    if (new_pal != pal) {
      data[linear_idx] = new_pal;
    }
  });
  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_REPLACE_H_