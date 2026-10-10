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

#ifndef FSCHEMA_FILTERS_BLOCK_NAME_FILTER_H_
#define FSCHEMA_FILTERS_BLOCK_NAME_FILTER_H_

#include <cstdint>
#include <string_view>
#include <vector>

#include "fschema/base/block_utils.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::filters {

/// Filters blocks matching a specific palette entry name.
class BlockNameFilter {
 public:
  BlockNameFilter(std::string_view name, const ir::Region& r) : name_(name) {
    matched_.assign(r.palette.size(), 0);
    any_matched_ = false;
    for (std::size_t i = 0; i < r.palette.size(); ++i) {
      if (r.palette[i].name == name) {
        matched_[i] = 1;
        any_matched_ = true;
      }
    }
  }

  [[nodiscard]] bool operator()(LocalPos /*p=*/,
                                std::uint16_t palette_idx) const noexcept {
    return palette_idx < matched_.size() && matched_[palette_idx];
  }

  [[nodiscard]] std::string_view name() const noexcept { return name_; }

  /// Returns true if at least one palette entry matches the name.
  [[nodiscard]] bool any_matched() const noexcept { return any_matched_; }

 private:
  std::string_view name_;
  std::vector<std::uint8_t> matched_;  // size = palette.size()
  bool any_matched_ = false;
};

/// Filters air-variant blocks (minecraft:air, cave_air, void_air).
///
/// Pre-computes a boolean LUT during construction for O(1) inner-loop queries.
class IsAirFilter {
 public:
  explicit IsAirFilter(const ir::Region& r) {
    matched_.assign(r.palette.size(), false);
    for (std::size_t i = 0; i < r.palette.size(); ++i) {
      if (fschema::base::IsAirVariant(r.palette[i].name)) {
        matched_[i] = true;
      }
    }
  }

  [[nodiscard]] bool operator()(LocalPos /*p=*/,
                                std::uint16_t palette_idx) const noexcept {
    return palette_idx < matched_.size() && matched_[palette_idx];
  }

 private:
  std::vector<std::uint8_t> matched_;  // size = palette.size()
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_BLOCK_NAME_FILTER_H_