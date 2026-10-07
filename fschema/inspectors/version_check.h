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

#ifndef FSCHEMA_INSPECTORS_VERSION_CHECK_H_
#define FSCHEMA_INSPECTORS_VERSION_CHECK_H_

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/filters/pos.h"
#include "fschema/filters/view.h"
#include "fschema/inspectors/internal/block_version_data.h"
#include "fschema/ir/types.h"

namespace fschema::inspectors {

/// Represents a block incompatible with a target Minecraft version.
struct IncompatibleBlock {
  filters::LocalPos pos;
  std::string_view block_name;
  std::string_view reason;
};

/// Finds all blocks incompatible with the target Java Edition DataVersion.
///
/// @param target_version The Minecraft DataVersion to validate against.
/// @return A list of incompatible block locations and failure reasons.
[[nodiscard]] inline std::vector<IncompatibleBlock> FindIncompatibleBlocks(
    const filters::View& v,
    std::int32_t target_version) {
  std::vector<IncompatibleBlock> result;
  const auto& table = internal::GetBlockVersionTable();
  const ir::Region& r = v.region();

  if (r.palette.empty()) return result;

  struct PaletteState {
    bool incompatible = false;
    std::string_view reason;
  };
  std::vector<PaletteState> states(r.palette.size());

  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    auto it = table.find(r.palette[i].name);
    if (it == table.end()) continue;

    const auto& info = it->second;
    if (target_version < info.added_java) {
      states[i] = {true, "Added in a later version"};
    } else if (target_version >= info.removed_java) {
      states[i] = {true, "Removed in this version"};
    }
  }

  v.for_each([&](filters::LocalPos p, std::uint16_t pal) {
    if (pal < states.size() && states[pal].incompatible) {
      result.push_back({p, r.palette[pal].name, states[pal].reason});
    }
  });

  return result;
}

/// Determines the minimum Minecraft DataVersion required to load the schematic.
[[nodiscard]] inline std::int32_t GetMinimumRequiredDataVersion(
    const filters::View& v) {
  std::int32_t min_required = 0;
  const auto& table = internal::GetBlockVersionTable();
  const ir::Region& r = v.region();

  if (r.palette.empty()) return 0;

  std::vector<std::uint8_t> used_palette(r.palette.size(), 0);
  v.for_each([&](filters::LocalPos, std::uint16_t pal) {
    if (pal < used_palette.size()) used_palette[pal] = 1;
  });

  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (!used_palette[i]) continue;
    auto it = table.find(r.palette[i].name);
    if (it != table.end()) {
      min_required = std::max(min_required, it->second.added_java);
    }
  }

  return min_required;
}

/// Calculates the highest Minecraft DataVersion fully supported
/// by the schematic.
///
/// Returns `std::numeric_limits<std::int32_t>::max()`
/// if no blocks in the schematic are ever removed.
[[nodiscard]] inline std::int32_t GetHighestSupportedDataVersion(
    const filters::View& v) {
  constexpr std::int32_t kIntMax = std::numeric_limits<std::int32_t>::max();
  const auto& table = internal::GetBlockVersionTable();
  const auto& valid_versions = internal::GetValidDataVersions();
  const ir::Region& r = v.region();

  if (r.palette.empty()) return kIntMax;

  std::vector<std::uint8_t> used_palette(r.palette.size(), 0);
  v.for_each([&](filters::LocalPos, std::uint16_t pal) {
    if (pal < used_palette.size()) used_palette[pal] = 1;
  });

  // Find the earliest version a used block was removed.
  std::int32_t min_removed = kIntMax;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (!used_palette[i]) continue;
    auto it = table.find(r.palette[i].name);
    if (it != table.end()) {
      min_removed = std::min(min_removed, it->second.removed_java);
    }
  }

  // Early Minecraft version guards (99 / 100).
  if (min_removed == 99 || min_removed == 100) {
    return 0;
  }

  if (min_removed == kIntMax) {
    return kIntMax;
  }

  // DataVersion is sparse; find the largest valid version strictly less than
  // `min_removed`.
  auto it = std::lower_bound(
      valid_versions.begin(), valid_versions.end(), min_removed);
  if (it == valid_versions.begin()) {
    return 0;
  }
  --it;
  return *it;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_VERSION_CHECK_H_