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

#include "fschema/schem/internal/block_state_string.h"

#include <string_view>

#include "fschema/schem/types.h"

namespace fschema::schem::internal {

void ParseBlockStateString(std::string_view full, BlockState& out) {
  out.full = full;

  const auto bracket = full.find('[');
  if (bracket == std::string_view::npos) {
    out.name = full;
    out.properties = {};
    return;
  }

  out.name = full.substr(0, bracket);

  const auto close = full.rfind(']');
  if (close == std::string_view::npos || close <= bracket) {
    out.properties = full.substr(bracket + 1);
    return;
  }

  out.properties = full.substr(bracket + 1, close - bracket - 1);
}

}  // namespace fschema::schem::internal