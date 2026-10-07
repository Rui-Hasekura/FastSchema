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

#include <cstdint>
#include <limits>

#include "fschema/filters/pos.h"

namespace fschema::filters {

/// Used by `RandomFilter` to provide stochastic block selection.
class Xoshiro256PP {
 public:
  explicit Xoshiro256PP(std::uint64_t seed = 0x9E3779B97F4A7C15ULL) noexcept {
    // SplitMix64 to initialize state from a single 64-bit seed
    std::uint64_t z = seed;
    for (int i = 0; i < 4; ++i) {
      z += 0x9E3779B97F4A7C15ULL;
      z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
      z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
      s_[i] = z ^ (z >> 31);
    }
  }

  [[nodiscard]] std::uint64_t operator()() noexcept {
    const std::uint64_t result = rotl(s_[0] + s_[3], 23) + s_[0];
    const std::uint64_t t = s_[1] << 17;

    s_[2] ^= s_[0];
    s_[3] ^= s_[1];
    s_[1] ^= s_[2];
    s_[0] ^= s_[3];

    s_[2] ^= t;
    s_[3] = rotl(s_[3], 45);

    return result;
  }

 private:
  static std::uint64_t rotl(const std::uint64_t x, int k) noexcept {
    return (x << k) | (x >> (64 - k));
  }
  std::uint64_t s_[4];
};

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

  [[nodiscard]] bool operator()(LocalPos /*p=*/,
                                std::uint16_t /*idx=*/) noexcept {
    if (threshold_ == 0) return false;
    if (threshold_ == std::numeric_limits<std::uint64_t>::max()) return true;
    return rng_() <= threshold_;
  }

 private:
  Xoshiro256PP rng_;
  std::uint64_t threshold_;
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_RANDOM_FILTER_H_