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

#include <oneapi/tbb/blocked_range.h>
#include <oneapi/tbb/concurrent_vector.h>
#include <oneapi/tbb/parallel_for.h>
#include <oneapi/tbb/parallel_reduce.h>

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

struct IncompatibleBlock {
  filters::LocalPos pos;
  std::string_view block_name;
  std::string_view reason;
};

namespace internal {

// Returns a bitmask of palette entries used by at least one block.
[[nodiscard]] inline std::vector<std::uint8_t> ComputeUsedPalette(
    const filters::View& v) {
  const ir::Region& r = v.region();
  const std::size_t pal_size = r.palette.size();
  std::vector<std::uint8_t> used(pal_size, 0);
  if (pal_size == 0) return used;

  const std::uint16_t* data = r.block_indices.data();
  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  constexpr std::uint64_t kParallelThreshold = 1 << 18;

  auto process_y = [&](std::int32_t y, std::vector<std::uint8_t>& local_used) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      for (std::int32_t x = 0; x < sx; ++x) {
        std::uint16_t pal = data[z_base + x];
        if (pal < pal_size) [[likely]]
          local_used[pal] = 1;
      }
    }
  };

  if (vol < kParallelThreshold) {
    for (std::int32_t y = 0; y < sy; ++y) process_y(y, used);
  } else {
    used = tbb::parallel_reduce(
        tbb::blocked_range<std::int32_t>(0, sy),
        std::vector<std::uint8_t>(pal_size, 0),
        [&](const tbb::blocked_range<std::int32_t>& range,
            std::vector<std::uint8_t> local_used) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            process_y(y, local_used);
          }
          return local_used;
        },
        [pal_size](std::vector<std::uint8_t> a,
                   const std::vector<std::uint8_t>& b) {
          for (std::size_t i = 0; i < pal_size; ++i) {
            a[i] = a[i] | b[i];
          }
          return a;
        });
  }
  return used;
}

}  // namespace internal

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

  bool any_incompatible = false;
  std::vector<std::uint8_t> is_target(r.palette.size() + 1, 0);
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (states[i].incompatible) {
      is_target[i] = 1;
      any_incompatible = true;
    }
  }
  if (!any_incompatible) return result;

  tbb::concurrent_vector<IncompatibleBlock> concurrent_result;

  const auto& bounds = r.bounds;
  const std::int32_t sx = bounds.size[0];
  const std::int32_t sy = bounds.size[1];
  const std::int32_t sz = bounds.size[2];
  const std::uint16_t* data = r.block_indices.data();
  const auto& filter = v.filter();

  const std::uint64_t vol = static_cast<std::uint64_t>(sx) * sy * sz;
  constexpr std::uint64_t kParallelThreshold = 1 << 18;  // 256K blocks
  constexpr std::size_t kBlockSize = 16;

  auto process_y = [&](std::int32_t y) {
    const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
    for (std::int32_t z = 0; z < sz; ++z) {
      const std::uint64_t z_base = y_base + static_cast<std::uint64_t>(z) * sx;
      std::int32_t x = 0;

      for (; x + kBlockSize <= sx; x += kBlockSize) {
        std::uint32_t filter_mask = 0;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
          if (filter({x + static_cast<std::int32_t>(i), y, z},
                     data[z_base + x + i])) {
            filter_mask |= (1u << i);
          }
        }
        if (filter_mask == 0) continue;

        std::uint16_t final_mask = 0;
        for (std::size_t i = 0; i < kBlockSize; ++i) {
          if (filter_mask & (1u << i)) {
            std::uint16_t pal = data[z_base + x + i];
            if (pal < is_target.size() && is_target[pal]) {
              final_mask |= (1u << i);
            }
          }
        }

        if (final_mask != 0) {
          for (std::size_t i = 0; i < kBlockSize; ++i) {
            if (final_mask & (1u << i)) {
              std::uint16_t pal = data[z_base + x + i];
              concurrent_result.push_back(
                  {{x + static_cast<std::int32_t>(i), y, z},
                   r.palette[pal].name,
                   states[pal].reason});
            }
          }
        }
      }

      for (; x < sx; ++x) {
        std::uint16_t pal = data[z_base + x];
        if (filter({x, y, z}, pal) && pal < is_target.size() &&
            is_target[pal]) {
          concurrent_result.push_back(
              {{x, y, z}, r.palette[pal].name, states[pal].reason});
        }
      }
    }
  };

  if (vol < kParallelThreshold) {
    for (std::int32_t y = 0; y < sy; ++y) {
      process_y(y);
    }
  } else {
    tbb::parallel_for(
        tbb::blocked_range<std::int32_t>(0, sy),
        [&](const tbb::blocked_range<std::int32_t>& range) {
          for (std::int32_t y = range.begin(); y < range.end(); ++y) {
            process_y(y);
          }
        },
        tbb::static_partitioner{});
  }

  result.reserve(concurrent_result.size());
  result.insert(
      result.end(), concurrent_result.begin(), concurrent_result.end());
  return result;
}

[[nodiscard]] inline std::int32_t GetMinimumRequiredDataVersion(
    const filters::View& v) {
  std::int32_t min_required = 0;
  const auto& table = internal::GetBlockVersionTable();
  const ir::Region& r = v.region();

  if (r.palette.empty()) return 0;

  const auto used_palette = internal::ComputeUsedPalette(v);

  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (!used_palette[i]) continue;
    auto it = table.find(r.palette[i].name);
    if (it != table.end()) {
      min_required = std::max(min_required, it->second.added_java);
    }
  }

  return min_required;
}

[[nodiscard]] inline std::int32_t GetHighestSupportedDataVersion(
    const filters::View& v) {
  constexpr std::int32_t kIntMax = std::numeric_limits<std::int32_t>::max();
  const auto& table = internal::GetBlockVersionTable();
  const auto& valid_versions = internal::GetValidDataVersions();
  const ir::Region& r = v.region();

  if (r.palette.empty()) return kIntMax;

  const auto used_palette = internal::ComputeUsedPalette(v);

  std::int32_t min_removed = kIntMax;
  for (std::size_t i = 0; i < r.palette.size(); ++i) {
    if (!used_palette[i]) continue;
    auto it = table.find(r.palette[i].name);
    if (it != table.end()) {
      min_removed = std::min(min_removed, it->second.removed_java);
    }
  }

  if (min_removed == 99 || min_removed == 100) {
    return 0;
  }

  if (min_removed == kIntMax) {
    return kIntMax;
  }

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