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

#ifndef FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_
#define FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_

#include <cstdint>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/base/error.h"
#include "fschema/editors/extract.h"
#include "fschema/filters/view.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

[[nodiscard]] inline ParseResult<ir::Region> BoundingBoxTrim(
    ir::Region& r,
    memory::Arena& arena) {
  auto mat_res = ir::EnsureMaterialized(r, arena);
  if (!mat_res) return std::unexpected(mat_res.error());

  const auto& bounds = r.bounds;
  const std::uint64_t vol = static_cast<std::uint64_t>(bounds.size[0]) *
                            bounds.size[1] * bounds.size[2];
  if (vol == 0) {
    ir::Region empty;
    empty.name = "trimmed_empty";
    empty.bounds = {{0, 0, 0}, {0, 0, 0}};
    empty.position = {0, 0, 0};
    empty.size = {0, 0, 0};
    empty.is_materialized = true;
    return empty;
  }

  const std::size_t pal_size = r.palette.size();
  std::vector<std::uint8_t> is_air(pal_size + 1, 0);
  for (std::size_t i = 0; i < pal_size; ++i) {
    if (base::IsAirVariant(r.palette[i].name)) {
      is_air[i] = 1;
    }
  }

  // Match all non-air blocks.
  auto not_air_filter = [&](filters::LocalPos, std::uint16_t pal) -> bool {
    return pal < pal_size && !is_air[pal];
  };

  auto view_res = filters::MakeView(r, not_air_filter, arena);
  if (!view_res) return std::unexpected(view_res.error());

  return Extract(*view_res, arena);
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_BOUNDING_BOX_TRIM_H_