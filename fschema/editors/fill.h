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

#ifndef FSCHEMA_EDITORS_FILL_H_
#define FSCHEMA_EDITORS_FILL_H_

#include <string_view>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/editors/palette_utils.h"
#include "fschema/filters/view.h"

namespace fschema::editors {

/// Sets every selected block to `block_name`.
///
/// If the block is not already in the palette, it is appended.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Fill(filters::BasicView<F> v,
                                     std::string_view block_name) {
  ir::Region& r = v.region();
  const std::uint16_t target_idx = ResolveOrAppend(r, block_name);
  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  v.for_each_linear([&](std::uint64_t linear_idx, std::uint16_t) {
    data[linear_idx] = target_idx;
  });
  MarkEdited(r);
  return {};
}

/// Sets every selected block to air.
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Delete(filters::BasicView<F> v) {
  ir::Region& r = v.region();
  const std::uint16_t air_idx = ResolveAir(r);
  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  v.for_each_linear([&](std::uint64_t linear_idx, std::uint16_t) {
    data[linear_idx] = air_idx;
  });
  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_FILL_H_