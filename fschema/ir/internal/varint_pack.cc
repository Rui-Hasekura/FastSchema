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

#include "fschema/ir/internal/varint_pack.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fschema::ir::internal {

namespace {

constexpr std::size_t kMinBlocksForParallel = 1 << 18;  // 256K blocks

inline std::byte* EncodeVarintScalar(std::uint16_t v, std::byte* dst) {
  if (v < 0x80) {
    *dst++ = static_cast<std::byte>(v);
  } else if (v < 0x4000) {
    *dst++ = static_cast<std::byte>((v & 0x7F) | 0x80);
    *dst++ = static_cast<std::byte>(v >> 7);
  } else {
    *dst++ = static_cast<std::byte>((v & 0x7F) | 0x80);
    *dst++ = static_cast<std::byte>(((v >> 7) & 0x7F) | 0x80);
    *dst++ = static_cast<std::byte>(v >> 14);
  }
  return dst;
}

inline constexpr std::size_t VarintSize(std::uint16_t v) {
  return (v < 0x80) ? 1 : (v < 0x4000) ? 2 : 3;
}

}  // namespace

std::vector<std::byte> PackVarintSchem(std::span<const std::uint16_t> indices) {
  if (indices.empty()) return {};

  const std::size_t n = indices.size();
  const std::uint16_t* src = indices.data();

  if (n < kMinBlocksForParallel) {
    std::vector<std::byte> out(n * 3);
    std::byte* dst = out.data();
    for (std::size_t i = 0; i < n; ++i) {
      dst = EncodeVarintScalar(src[i], dst);
    }
    out.resize(static_cast<std::size_t>(dst - out.data()));
    return out;
  }

  constexpr std::size_t kChunkSize = 1 << 16;  // 64K blocks per chunk
  const std::size_t num_chunks = (n + kChunkSize - 1) / kChunkSize;

  // Phase 1: parallel byte count
  std::vector<std::size_t> chunk_bytes(num_chunks);

  auto count_range = [&](const tbb::blocked_range<std::size_t>& range) {
    for (std::size_t c = range.begin(); c < range.end(); ++c) {
      const std::size_t start = c * kChunkSize;
      const std::size_t end = std::min(start + kChunkSize, n);
      std::size_t sz = 0;
      for (std::size_t i = start; i < end; ++i) {
        sz += VarintSize(src[i]);
      }
      chunk_bytes[c] = sz;
    }
  };

  tbb::parallel_for(tbb::blocked_range<std::size_t>(0, num_chunks),
                    count_range,
                    tbb::static_partitioner{});

  // Phase 2: prefix-sum (serial)
  std::vector<std::size_t> offsets(num_chunks + 1);
  offsets[0] = 0;
  for (std::size_t c = 0; c < num_chunks; ++c) {
    offsets[c + 1] = offsets[c] + chunk_bytes[c];
  }
  const std::size_t total = offsets[num_chunks];

  // Phase 3: parallel fill — each chunk writes to out[offsets[c]..]
  std::vector<std::byte> out(total);

  auto fill_range = [&](const tbb::blocked_range<std::size_t>& range) {
    for (std::size_t c = range.begin(); c < range.end(); ++c) {
      const std::size_t start = c * kChunkSize;
      const std::size_t end = std::min(start + kChunkSize, n);
      std::byte* dst = out.data() + offsets[c];
      for (std::size_t i = start; i < end; ++i) {
        dst = EncodeVarintScalar(src[i], dst);
      }
    }
  };

  tbb::parallel_for(tbb::blocked_range<std::size_t>(0, num_chunks),
                    fill_range,
                    tbb::static_partitioner{});

  return out;
}

}  // namespace fschema::ir::internal