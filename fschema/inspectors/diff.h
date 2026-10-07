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

#ifndef FSCHEMA_INSPECTORS_DIFF_H_
#define FSCHEMA_INSPECTORS_DIFF_H_

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string_view>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "fschema/base/error.h"
#include "fschema/base/hash.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"

namespace fschema::inspectors {

/// Sentinel: a palette entry in r2 that has no match in r1.
constexpr std::uint16_t kUnmappedPalette = 0xFFFF;

namespace {

struct BlockStateKey {
  std::string_view name;
  std::span<const std::byte> props;
};

struct BlockStateKeyHash {
  std::size_t operator()(const BlockStateKey& k) const noexcept {
    std::uint64_t h1 = base::Hash64WithSeed(k.name.data(), k.name.size());
    std::uint64_t h2 = base::Hash64WithSeed(k.props.data(), k.props.size(), h1);
    return static_cast<std::size_t>(h2);
  }
};

struct BlockStateKeyEq {
  bool operator()(const BlockStateKey& a,
                  const BlockStateKey& b) const noexcept {
    if (a.name != b.name) return false;
    if (a.props.size() != b.props.size()) return false;
    return std::equal(a.props.begin(), a.props.end(), b.props.begin());
  }
};

}  // anonymous namespace

[[nodiscard]] inline std::vector<std::uint16_t> BuildBToAMap(
    const ir::Region& ra,
    const ir::Region& rb) {
  std::vector<std::uint16_t> b_to_a(rb.palette.size(), kUnmappedPalette);

  absl::flat_hash_map<BlockStateKey,
                      std::uint16_t,
                      BlockStateKeyHash,
                      BlockStateKeyEq>
      map_a;
  map_a.reserve(ra.palette.size());
  for (std::size_t i = 0; i < ra.palette.size(); ++i) {
    map_a.insert({{ra.palette[i].name, ra.palette[i].raw_properties},
                  static_cast<std::uint16_t>(i)});
  }

  for (std::size_t j = 0; j < rb.palette.size(); ++j) {
    auto it = map_a.find({rb.palette[j].name, rb.palette[j].raw_properties});
    if (it != map_a.end()) {
      b_to_a[j] = it->second;
    }
  }

  return b_to_a;
}

/// Compares two regions and returns a list of differing blocks.
///
/// Preconditions:
///   Both regions must have the EXACT same `BoundingBox` (origin and size).
///
/// Matching is done by (name, raw_properties), NOT by raw palette index.
/// This makes it robust to palette reordering between two regions.
///
/// Semantics:
///   - A block in r2 whose palette entry has no match in r1
///     (`kUnmappedPalette`) is always reported as a difference.
///   - Property encodings (kNbt / kString) must match. Cross-format
///     comparisons require normalizing properties first.
[[nodiscard]] inline ParseResult<std::vector<DiffEntry>> Diff(
    const ir::Region& r1,
    const ir::Region& r2) {
  for (int i = 0; i < 3; ++i) {
    if (r1.bounds.size[i] != r2.bounds.size[i] ||
        r1.bounds.origin[i] != r2.bounds.origin[i]) {
      return std::unexpected(ParseError{
          ParseError::Code::InvalidTagId,
          "DiffInspector: Regions must have the same size and origin",
          0});
    }
  }

  const auto b_to_a = BuildBToAMap(r1, r2);

  std::vector<DiffEntry> result;
  const std::uint64_t vol = static_cast<std::uint64_t>(r1.bounds.size[0]) *
                            static_cast<std::uint64_t>(r1.bounds.size[1]) *
                            static_cast<std::uint64_t>(r1.bounds.size[2]);

  const std::int32_t sx = r1.bounds.size[0];
  const std::int32_t sz = r1.bounds.size[2];

  for (std::uint64_t i = 0; i < vol; ++i) {
    const std::uint16_t p1 = r1.block_indices[i];
    const std::uint16_t p2 = r2.block_indices[i];

    const std::uint16_t p2_mapped =
        (p2 < b_to_a.size()) ? b_to_a[p2] : kUnmappedPalette;

    if (p1 == p2_mapped) continue;

    const std::int32_t y =
        static_cast<std::int32_t>(i / (static_cast<std::uint64_t>(sx) * sz));
    const std::uint64_t rem = i % (static_cast<std::uint64_t>(sx) * sz);
    const std::int32_t z = static_cast<std::int32_t>(rem / sx);
    const std::int32_t x = static_cast<std::int32_t>(rem % sx);

    const std::string_view n1 =
        (p1 < r1.palette.size()) ? r1.palette[p1].name : "unknown";
    const std::string_view n2 =
        (p2 < r2.palette.size()) ? r2.palette[p2].name : "unknown";

    result.push_back({{x, y, z}, n1, n2});
  }
  return result;
}

}  // namespace fschema::inspectors

#endif  // FSCHEMA_INSPECTORS_DIFF_H_