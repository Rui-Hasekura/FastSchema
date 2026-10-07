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

#ifndef FSCHEMA_FILTERS_BLOCK_STATE_FILTER_H_
#define FSCHEMA_FILTERS_BLOCK_STATE_FILTER_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "fschema/base/nbt/property.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::filters {

/// A key-value condition applied to a BlockState property.
struct BlockStateCondition {
  std::string_view key;
  std::string_view value;
};

/// Filters blocks matching all specified BlockState conditions.
class BlockStateFilter {
 public:
  BlockStateFilter(const ir::Region& r,
                   std::vector<BlockStateCondition> conditions)
      : matched_(r.palette.size(), 0) {
    for (std::size_t i = 0; i < r.palette.size(); ++i) {
      const auto& bs = r.palette[i];
      if (bs.raw_properties.empty()) continue;

      bool all_match = true;
      for (const auto& cond : conditions) {
        bool matched = false;
        if (bs.prop_encoding == ir::PropertyEncoding::kString) {
          matched = MatchStringProp(bs.raw_properties, cond.key, cond.value);
        } else if (bs.prop_encoding == ir::PropertyEncoding::kNbt) {
          matched = MatchNbtProp(bs.raw_properties, cond.key, cond.value);
        }
        if (!matched) {
          all_match = false;
          break;
        }
      }
      if (all_match) {
        matched_[i] = 1;
      }
    }
  }

  [[nodiscard]] bool operator()(LocalPos /*p*/,
                                std::uint16_t palette_idx) const noexcept {
    return palette_idx < matched_.size() && matched_[palette_idx] != 0;
  }

 private:
  // Matches key=value in a comma-separated property string ("k1=v1,k2=v2").
  [[nodiscard]] static bool MatchStringProp(std::span<const std::byte> props,
                                            std::string_view key,
                                            std::string_view value) {
    std::string_view p(reinterpret_cast<const char*>(props.data()),
                       props.size());
    std::size_t pos = 0;
    while (pos < p.size()) {
      auto next_comma = p.find(',', pos);
      std::string_view entry = (next_comma == std::string_view::npos)
                                   ? p.substr(pos)
                                   : p.substr(pos, next_comma - pos);

      auto eq_pos = entry.find('=');
      if (eq_pos != std::string_view::npos) {
        if (entry.substr(0, eq_pos) == key &&
            entry.substr(eq_pos + 1) == value) {
          return true;
        }
      }
      if (next_comma == std::string_view::npos) break;
      pos = next_comma + 1;
    }
    return false;
  }

  // Matches a String tag entry inside a raw NBT Compound payload.
  [[nodiscard]] static bool MatchNbtProp(std::span<const std::byte> props,
                                         std::string_view key,
                                         std::string_view value) {
    bool found = false;
    base::ForEachNbtStringProperty(props,
                                   [&](std::string_view k, std::string_view v) {
                                     if (k == key && v == value) {
                                       found = true;
                                       return false;
                                     }
                                     return true;
                                   });
    return found;
  }

  std::vector<std::uint8_t> matched_;
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_BLOCK_STATE_FILTER_H_