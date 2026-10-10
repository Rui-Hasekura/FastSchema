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

#ifndef FSCHEMA_EDITORS_PALETTE_UTILS_H_
#define FSCHEMA_EDITORS_PALETTE_UTILS_H_

#include <cstdint>
#include <optional>
#include <string_view>

#include "fschema/base/block_utils.h"
#include "fschema/ir/types.h"

namespace fschema::editors {

/// Finds the first palette index whose `BlockState::name`
/// matches the given name.
///
/// Complexity: O(palette_size).
/// @return The index if found, otherwise `std::nullopt`.
[[nodiscard]] inline std::optional<std::uint16_t> FindPaletteIndex(
    const ir::Region& r,
    std::string_view name) noexcept {
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (r.palette[i].name == name) {
      return static_cast<std::uint16_t>(i);
    }
  }
  return std::nullopt;
}

/// Finds the palette index to use when writing "air".
[[nodiscard]] inline std::optional<std::uint16_t> FindAirIndex(
    const ir::Region& r) noexcept {
  std::optional<std::uint16_t> fallback;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    const auto& name = r.palette[i].name;
    if (name == "minecraft:air") {
      return static_cast<std::uint16_t>(i);
    }
    if (!fallback && fschema::base::IsAirVariant(name)) {
      fallback = static_cast<std::uint16_t>(i);
    }
  }
  return fallback;
}

/// Appends a new `BlockState` (with no properties) to the palette.
///
/// @pre `r.palette.size() < 65536` (hard limit imposed by `uint16_t` indices).
/// @return The index of the newly appended entry.
[[nodiscard]] inline std::uint16_t AppendPaletteEntry(ir::Region& r,
                                                      std::string_view name) {
  ir::BlockState bs;
  bs.name = name;
  bs.prop_encoding = ir::PropertyEncoding::kNone;
  r.palette.push_back(bs);
  r.is_air_lut.clear();
  return static_cast<std::uint16_t>(r.palette.size() - 1);
}

/// Finds a palette index by name; if absent, appends a new entry.
///
/// Side Effects:
/// If a new entry is appended,
/// `r.lazy_source.palette_pristine` is set to false.
/// This invalidates the encoder's lazy passthrough path,
/// forcing a full repack of the block indices during serialization.
[[nodiscard]] inline std::uint16_t ResolveOrAppend(ir::Region& r,
                                                   std::string_view name) {
  if (auto idx = FindPaletteIndex(r, name)) return *idx;
  r.lazy_source.palette_pristine = false;
  return AppendPaletteEntry(r, name);
}

/// Finds an air palette index; if absent, appends "minecraft:air".
/// @see `ResolveOrAppend` for side effects.
[[nodiscard]] inline std::uint16_t ResolveAir(ir::Region& r) {
  if (auto idx = FindAirIndex(r)) return *idx;
  r.lazy_source.palette_pristine = false;
  return AppendPaletteEntry(r, "minecraft:air");
}

/// Marks the region as structurally modified.
///
/// Must be called by any editor that mutates `block_indices`.
/// Sets `edited = true` and invalidates `palette_pristine`,
/// forcing the encoder to
/// re-serialize from the materialized `block_indices` array.
inline void MarkEdited(ir::Region& r) noexcept {
  r.edited = true;
  r.lazy_source.palette_pristine = false;
}

}  // namespace fschema::editors

#endif  // FSCHEMA_EDITORS_PALETTE_UTILS_H_