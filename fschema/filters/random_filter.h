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

#ifndef FSCHEMA_FILTERS_RANDOM_FILTER_H_
#define FSCHEMA_FILTERS_RANDOM_FILTER_H_

#include <limits>

#include "fschema/base/random.h"
#include "fschema/filters/pos.h"

namespace fschema::filters {

/// Selects blocks with a given probability.
///
/// Converts probability to integer threshold on construction;
/// inner loop is integer-only.
class RandomFilter {
 public:
  explicit RandomFilter(double probability,
                        std::uint64_t seed = 0x12345678ULL) noexcept
      : rng_(seed) {
    if (probability <= 0.0) {
      threshold_ = 0;
    } else if (probability >= 1.0) {
      threshold_ = std::numeric_limits<std::uint64_t>::max();
    } else {
      threshold_ = static_cast<std::uint64_t>(
          probability *
          static_cast<double>(std::numeric_limits<std::uint64_t>::max()));
    }
  }

  [[nodiscard]] bool operator()(LocalPos /*p*/,
                                std::uint16_t /*idx*/) const noexcept {
    if (threshold_ == 0) return false;
    if (threshold_ == std::numeric_limits<std::uint64_t>::max()) return true;
    return rng_() <= threshold_;
  }

 private:
  mutable base::Xoshiro256PP rng_;
  std::uint64_t threshold_;
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_RANDOM_FILTER_H_