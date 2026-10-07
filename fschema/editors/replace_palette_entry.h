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

#ifndef FSCHEMA_EDITORS_REPLACE_PALETTE_ENTRY_H_
#define FSCHEMA_EDITORS_REPLACE_PALETTE_ENTRY_H_

#include <cstdint>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/ir/types.h"

namespace fschema::editors {

/// Replaces the `BlockState` at `palette[target_idx]` with a new name,
/// clearing any existing properties.
///
/// Target_idx cannot be 0 (protects air slot).
/// new_name must outlive Region.
///
/// This operation does not remap block_indices;
/// the change is visible immediately. palette_pristine is left untouched,
/// so the encoder may still reuse packed data.
[[nodiscard]] inline ParseResult<void> ReplacePaletteEntry(
    ir::Region& r,
    std::uint16_t target_idx,
    std::string_view new_name) {
  if (target_idx == 0) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::PaletteIndexOutOfRange,
                   "ReplacePaletteEntry: index 0 is protected (air slot)",
                   0});
  }
  if (target_idx >= r.palette.size()) [[unlikely]] {
    return std::unexpected(ParseError{ParseError::Code::PaletteIndexOutOfRange,
                                      "ReplacePaletteEntry: index out of range",
                                      0});
  }

  ir::BlockState& bs = r.palette[target_idx];
  bs.name = new_name;
  bs.raw_properties = {};  // clear properties
  bs.prop_encoding = ir::PropertyEncoding::kNone;
  return {};
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_REPLACE_PALETTE_ENTRY_H_