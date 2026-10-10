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

#include <tbb/blocked_range.h>
#include <tbb/parallel_reduce.h>

#include <cstddef>
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
  std::string properties;  // Decoded "k=v,k=v"; empty if no properties.
  std::uint64_t count;
};

[[nodiscard]] inline std::vector<MaterialEntry> ListMaterial(
    const ir::Region& r,
    bool use_sort = false) {
  const std::size_t pal_size = r.palette.size();
  if (pal_size == 0) return {};

  const std::size_t vol = r.block_indices.size();
  std::vector<std::uint64_t> counts(pal_size, 0);

  constexpr std::size_t kParallelThreshold = 1 << 18;  // 256K blocks

  if (vol < kParallelThreshold) {
    for (std::size_t i = 0; i < vol; ++i) {
      std::uint16_t idx = r.block_indices[i];
      if (idx < pal_size) [[likely]]
        ++counts[idx];
    }
  } else {
    // Use local histograms to avoid contention on the shared counts vector.
    counts = tbb::parallel_reduce(
        tbb::blocked_range<std::size_t>(0, vol, kParallelThreshold),
        std::vector<std::uint64_t>(pal_size, 0),
        [&](const tbb::blocked_range<std::size_t>& range,
            std::vector<std::uint64_t> local_counts) {
          for (std::size_t i = range.begin(); i < range.end(); ++i) {
            std::uint16_t idx = r.block_indices[i];
            if (idx < pal_size) [[likely]]
              ++local_counts[idx];
          }
          return local_counts;
        },
        [pal_size](std::vector<std::uint64_t> a,
                   const std::vector<std::uint64_t>& b) {
          for (std::size_t i = 0; i < pal_size; ++i) {
            a[i] += b[i];
          }
          return a;
        });
  }

  std::vector<MaterialEntry> entries;
  entries.reserve(pal_size);
  for (std::size_t i = 0; i < pal_size; ++i) {
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