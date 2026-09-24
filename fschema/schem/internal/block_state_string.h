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

#ifndef FSCHEMA_SCHEM_INTERNAL_BLOCK_STATE_STRING_H_
#define FSCHEMA_SCHEM_INTERNAL_BLOCK_STATE_STRING_H_

#include <string_view>

#include "fschema/schem/types.h"

namespace fschema::schem::internal {

  void ParseBlockStateString(std::string_view full, BlockState& out);

  template <typename F>
  void ForEachProperty(std::string_view properties, F&& callback) {
    if (properties.empty()) return;

    std::size_t start = 0;
    while (start < properties.size()) {
      const auto comma = properties.find(',', start);
      const auto end = (comma == std::string_view::npos)
        ? properties.size()
        : comma;

      const auto pair = properties.substr(start, end - start);
      const auto eq = pair.find('=');
      if (eq != std::string_view::npos) {
        callback(pair.substr(0, eq), pair.substr(eq + 1));
      }

      start = end + 1;
    }
  }

}  // namespace fschema::schem::internal

#endif  // FSCHEMA_SCHEM_INTERNAL_BLOCK_STATE_STRING_H_