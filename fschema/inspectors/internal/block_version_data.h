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

// Note: Block names and DataVersions are factual data used for
// schematic compatibility and version checking.
// Minecraft is a trademark of Mojang Synergies AB.

#ifndef FSCHEMA_INSPECTORS_INTERNAL_BLOCK_VERSION_DATA_H_
#define FSCHEMA_INSPECTORS_INTERNAL_BLOCK_VERSION_DATA_H_

#include <cstdint>
#include <limits>
#include <vector>

#include "absl/container/flat_hash_map.h"

namespace fschema::inspectors::internal {

struct BlockVersionInfo {
  std::int32_t added_java = 0;
  std::int32_t removed_java = std::numeric_limits<std::int32_t>::max();
};

inline const absl::flat_hash_map<std::string_view, BlockVersionInfo>&
GetBlockVersionTable() {
  static const absl::flat_hash_map<std::string_view, BlockVersionInfo> table = {
#include "fschema/inspectors/internal/block_versions.inc"
  };
  return table;
}

// A sorted list of all valid Java Edition DataVersions.
inline const std::vector<std::int32_t>& GetValidDataVersions() {
  static const std::vector<std::int32_t> versions = {
#include "fschema/inspectors/internal/data_versions.inc"
  };
  return versions;
}

}  // namespace fschema::inspectors::internal

#endif  // FSCHEMA_INSPECTORS_INTERNAL_BLOCK_VERSION_DATA_H_