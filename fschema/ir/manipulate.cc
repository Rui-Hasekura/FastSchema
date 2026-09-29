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

#include "fschema/ir/manipulate.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "absl/container/flat_hash_map.h"
#include "fschema/base/error.h"
#include "fschema/base/hash.h"

namespace fschema::ir {

ParseResult<void> ExtractRegion(Schema& ir, std::string_view region_name) {
  auto it =
      std::find_if(ir.regions.begin(), ir.regions.end(), [&](const Region& r) {
        return r.name == region_name;
      });
  if (it == ir.regions.end()) {
    return std::unexpected(
        ParseError{ParseError::Code::MissingField, "ExtractRegion", 0});
  }
  Region extracted = std::move(*it);
  ir.regions.clear();
  ir.regions.push_back(std::move(extracted));
  ir.source_version = 0;
  return {};
}

namespace {

// Hashable key for BlockState dedup in MergeRegions.
struct BlockStateKey {
  std::string_view name;
  std::span<const std::byte> props;
};

struct BlockStateKeyHash {
  std::size_t operator()(const BlockStateKey& k) const noexcept {
    std::uint64_t h1 = base::FastHash3_64(k.name.data(), k.name.size());
    std::uint64_t h2 = base::FastHash3_64(k.props.data(), k.props.size(), h1);
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

}  // namespace

ParseResult<Region> MergeRegions(const std::vector<Region>& regions,
                                 std::string_view fill_block_name,
                                 memory::Arena& arena) {
  if (regions.empty()) {
    return std::unexpected(
        ParseError{ParseError::Code::MissingField, "Regions", 0});
  }

  // 1. Find global bounding box
  const Region& first = regions[0];
  std::int32_t min_x = first.bounds.origin[0];
  std::int32_t min_y = first.bounds.origin[1];
  std::int32_t min_z = first.bounds.origin[2];
  std::int32_t max_x = first.bounds.origin[0] + first.bounds.size[0];
  std::int32_t max_y = first.bounds.origin[1] + first.bounds.size[1];
  std::int32_t max_z = first.bounds.origin[2] + first.bounds.size[2];

  for (size_t i = 1; i < regions.size(); ++i) {
    const auto& r = regions[i];
    min_x = std::min(min_x, r.bounds.origin[0]);
    min_y = std::min(min_y, r.bounds.origin[1]);
    min_z = std::min(min_z, r.bounds.origin[2]);
    max_x = std::max(max_x, r.bounds.origin[0] + r.bounds.size[0]);
    max_y = std::max(max_y, r.bounds.origin[1] + r.bounds.size[1]);
    max_z = std::max(max_z, r.bounds.origin[2] + r.bounds.size[2]);
  }

  Region merged;
  merged.name = "main";
  merged.bounds.origin[0] = min_x;
  merged.bounds.origin[1] = min_y;
  merged.bounds.origin[2] = min_z;
  merged.bounds.size[0] = max_x - min_x;
  merged.bounds.size[1] = max_y - min_y;
  merged.bounds.size[2] = max_z - min_z;
  merged.index_order = IndexOrder::kYzx;

  // 2. Build merged palette with absl::flat_hash_map dedup.
  //    Index 0 is always the fill block (e.g. minecraft:air).
  merged.palette.push_back({fill_block_name, {}, PropertyEncoding::kNone});

  absl::flat_hash_map<BlockStateKey,
                      std::uint16_t,
                      BlockStateKeyHash,
                      BlockStateKeyEq>
      lookup;
  lookup.reserve(regions.size() * 64);
  lookup.try_emplace({merged.palette[0].name, merged.palette[0].raw_properties},
                     std::uint16_t{0});

  std::vector<std::vector<std::uint16_t>> remaps(regions.size());

  for (size_t i = 0; i < regions.size(); ++i) {
    remaps[i].resize(regions[i].palette.size());
    for (size_t j = 0; j < regions[i].palette.size(); ++j) {
      const auto& bs = regions[i].palette[j];
      if (bs.name == fill_block_name) {
        remaps[i][j] = 0;
        continue;
      }
      BlockStateKey key{bs.name, bs.raw_properties};
      if (auto it = lookup.find(key); it != lookup.end()) {
        remaps[i][j] = it->second;
      } else {
        const std::uint16_t new_idx =
            static_cast<std::uint16_t>(merged.palette.size());
        merged.palette.push_back(bs);
        lookup.try_emplace(key, new_idx);
        remaps[i][j] = new_idx;
      }
    }
  }

  // 3. Allocate and fill with air (index 0)
  std::uint64_t vol = static_cast<std::uint64_t>(merged.bounds.size[0]) *
                      static_cast<std::uint64_t>(merged.bounds.size[1]) *
                      static_cast<std::uint64_t>(merged.bounds.size[2]);
  merged.block_indices.resize_uninitialized(vol, &arena);
  std::memset(merged.block_indices.data(), 0, vol * sizeof(std::uint16_t));

  // 4. Copy blocks (YZX order)
  std::vector<std::uint16_t> scratch;
  const std::int32_t W = merged.bounds.size[0];
  const std::int32_t L = merged.bounds.size[2];
  std::uint16_t* const merged_base = merged.block_indices.data();

  for (size_t r = 0; r < regions.size(); ++r) {
    const auto& reg = regions[r];
    std::int32_t sx = reg.bounds.size[0];
    std::int32_t sy = reg.bounds.size[1];
    std::int32_t sz = reg.bounds.size[2];
    std::int32_t off_x = reg.bounds.origin[0] - min_x;
    std::int32_t off_y = reg.bounds.origin[1] - min_y;
    std::int32_t off_z = reg.bounds.origin[2] - min_z;

    const std::size_t reg_n = reg.block_indices.size();

    if (scratch.size() < reg_n) scratch.resize(reg_n);
    {
      const std::uint16_t* remap = remaps[r].data();
      const std::uint16_t* src = reg.block_indices.data();
      std::uint16_t* dst = scratch.data();
      std::size_t i = 0;
      for (; i + 4 <= reg_n; i += 4) {
        const std::uint16_t v0 = src[i + 0];
        const std::uint16_t v1 = src[i + 1];
        const std::uint16_t v2 = src[i + 2];
        const std::uint16_t v3 = src[i + 3];
        dst[i + 0] = remap[v0];
        dst[i + 1] = remap[v1];
        dst[i + 2] = remap[v2];
        dst[i + 3] = remap[v3];
      }
      // Scalar tail
      for (; i < reg_n; ++i) {
        dst[i] = remap[src[i]];
      }
    }

    const std::size_t row_bytes =
        static_cast<std::size_t>(sx) * sizeof(std::uint16_t);
    for (int y = 0; y < sy; ++y) {
      const std::uint64_t local_y_base =
          static_cast<std::uint64_t>(y) * (static_cast<std::uint64_t>(sx) * sz);
      const std::uint64_t global_y_base =
          static_cast<std::uint64_t>(y + off_y) *
              (static_cast<std::uint64_t>(W) * L) +
          static_cast<std::uint64_t>(off_z) * W +
          static_cast<std::uint64_t>(off_x);

      const std::uint16_t* src_row = scratch.data() + local_y_base;
      std::uint16_t* dst_row = merged_base + global_y_base;
      for (int z = 0; z < sz; ++z) {
        std::memcpy(dst_row, src_row, row_bytes);
        src_row += sx;  // advance to next z-slice in local (stride = sx)
        dst_row += W;   // advance to next z-slice in global (stride = W)
      }
    }

    // Merge entities and block entities
    for (const auto& be : reg.block_entities) {
      BlockEntity nbe = be;
      nbe.block_position[0] += off_x;
      nbe.block_position[1] += off_y;
      nbe.block_position[2] += off_z;
      merged.block_entities.push_back(std::move(nbe));
    }
    for (const auto& ent : reg.entities) {
      Entity nent = ent;
      nent.position[0] += off_x;
      nent.position[1] += off_y;
      nent.position[2] += off_z;
      merged.entities.push_back(std::move(nent));
    }
  }

  return merged;
}

}  // namespace fschema::ir