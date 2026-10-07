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

#ifndef FSCHEMA_INSPECTORS_LIST_MATERIAL_H_
#define FSCHEMA_INSPECTORS_LIST_MATERIAL_H_

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "fschema/ir/internal/codec_utils.h"
#include "fschema/ir/types.h"
#include "fschema/third_party/pdqsort/pdqsort.h"

namespace fschema::inspectors {

struct MaterialEntry {
  std::uint16_t palette_idx;
  std::string_view name;
  std::string properties;  // Decoded "k=v,k=v", empty if none
  std::uint64_t count;
};

/// Enumerates palette entries with per-entry block counts.
///
/// Converts block properties (Litematica NBT / Schem string) to normalized
/// strings.
///
/// @pre `r.is_materialized` should be true; unmaterialized regions result in 0
/// counts.
///
/// Complexity: O(volume + K) time, O(K) space (K = palette size).
/// With `use_sort`, additional O(K log K) sorting.
[[nodiscard]] inline std::vector<MaterialEntry> ListMaterial(
    const ir::Region& r,
    bool use_sort = false) {
  // Dense array LUT indexed by palette index (O(1) lookup)
  std::vector<std::uint64_t> counts(r.palette.size(), 0);
  for (std::uint16_t idx : r.block_indices) {
    if (idx < counts.size()) [[likely]]
      ++counts[idx];
  }

  std::vector<MaterialEntry> entries;
  entries.reserve(r.palette.size());
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    const auto& bs = r.palette[i];
    std::string props;
    if (bs.prop_encoding == ir::PropertyEncoding::kNbt &&
        !bs.raw_properties.empty()) {
      props = ir::internal::NbtPropsToString(bs.raw_properties);
    } else if (bs.prop_encoding == ir::PropertyEncoding::kString &&
               !bs.raw_properties.empty()) {
      props.assign(reinterpret_cast<const char*>(bs.raw_properties.data()),
                   bs.raw_properties.size());
    }
    entries.push_back(MaterialEntry{
        static_cast<std::uint16_t>(i),
        bs.name,
        std::move(props),
        counts[i],
    });
  }

  if (use_sort) {
    pdqsort(entries.begin(),
            entries.end(),
            [](const MaterialEntry& a, const MaterialEntry& b) {
              return a.count > b.count;
            });
  }
  return entries;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_LIST_MATERIAL_H_