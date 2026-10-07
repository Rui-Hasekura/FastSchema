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

#ifndef FSCHEMA_FILTERS_POS_H_
#define FSCHEMA_FILTERS_POS_H_

#include <cstdint>

#include "fschema/base/hash.h"

namespace fschema::filters {

// Represents a block coordinate relative to a region's origin.

// Invariants:
// 1. For a valid position inside a region with bounds `B`,
//    the coordinates satisfy `0 <= x < B.size[0]`,
//                            `0 <= y < B.size[1]`,
//                            `0 <= z < B.size[2]`.
// 2. Filters and editors receiving a LocalPos
//    generally assume it is in-bounds.
//    Exceptions are internal neighbor-checking utilities (e.g., `IsAirAt`)
//    which explicitly handle out-of-bounds coordinates as "air".
struct LocalPos {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;

  // Defaulted equality operator required for usage as a key in
  // `absl::flat_hash_set` and `absl::flat_hash_map`.
  friend bool operator==(const LocalPos&, const LocalPos&) = default;
};

// Spatial axes. The underlying integer values (0, 1, 2) deliberately
// match the array indices of `BoundingBox::size`/`origin`.
// This allows direct array indexing in generic logic:
//   `const auto& size = bounds.size[static_cast<int>(axis)];`
enum class Axis : std::uint8_t { X = 0, Y = 1, Z = 2 };

// Comparison operations used by spatial filters (e.g., `AxisFilter`).
enum class Cmp : std::uint8_t {
  Lt,  // <
  Le,  // <=
  Ge,  // >=
  Gt,  // >
  Eq,  // ==
  Ne,  // !=
};

// High-performance hash functor for `LocalPos` using XXH3.

// `LocalPos` is exactly 12 bytes (3x int32). Small enough for XXH3.
struct LocalPosHash {
  [[nodiscard]] std::size_t operator()(const LocalPos& p) const noexcept {
    return static_cast<std::size_t>(
        fschema::base::Hash64WithSeed(&p, sizeof(p)));
  }
};

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_POS_H_