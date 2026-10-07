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

#ifndef FSCHEMA_INSPECTORS_COUNT_H_
#define FSCHEMA_INSPECTORS_COUNT_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "fschema/filters/view.h"

namespace fschema::inspectors {

/// Counts the number of selected blocks whose palette name matches `name`.
template <filters::Filter F>
[[nodiscard]] std::size_t Count(const filters::BasicView<F>& v,
                                std::string_view name) {
  const ir::Region& r = v.region();
  std::vector<std::uint8_t> is_target(r.palette.size(), 0);
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (r.palette[i].name == name) is_target[i] = 1;
  }
  std::size_t n = 0;
  v.for_each([&](filters::LocalPos, std::uint16_t pal) {
    if (pal < is_target.size() && is_target[pal]) ++n;
  });
  return n;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_COUNT_H_