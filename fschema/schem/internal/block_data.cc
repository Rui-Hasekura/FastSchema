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

// NOTE: This is a **pure scalar** translation unit.  All SIMD code lives
// in simd_block_data.cc (the sole foreach_target file for this module,
// mirroring the litematic simd_block_states.cc / block_states.cc split).

#include "fschema/schem/internal/block_data.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <thread>
#include <utility>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/noinit_allocator.h"

namespace fschema::schem::internal {

namespace {

[[nodiscard]] inline ParseResult<std::uint32_t> DecodeOneVarint(
    const std::uint8_t*& p,
    const std::uint8_t* end) {
  if (p >= end) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::Truncated, "BlockData", 0});
  }
  std::uint32_t value = *p++;
  if (value < 0x80) [[likely]] {
    return value;
  }
  value &= 0x7F;
  for (int shift = 7; shift <= 28; shift += 7) {
    if (p >= end) [[unlikely]] {
      return std::unexpected(
          ParseError{ParseError::Code::Truncated, "BlockData", 0});
    }
    const std::uint8_t byte = *p++;
    value |= (byte & 0x7F) << shift;
    if (byte < 0x80) {
      return value;
    }
  }
  return std::unexpected(
      ParseError{ParseError::Code::VarintOverflow, "BlockData", 0});
}

[[nodiscard]] std::pair<const std::uint8_t*, std::uint64_t> FindVarintBoundary(
    const std::uint8_t* data_base,
    const std::uint8_t* data_end,
    const std::vector<std::uint64_t>& cumsum,
    std::size_t granularity,
    std::uint64_t target) {
  std::size_t lo = 0, hi = cumsum.size() - 1;
  while (lo < hi) {
    std::size_t mid = lo + (hi - lo + 1) / 2;
    if (cumsum[mid] <= target)
      lo = mid;
    else
      hi = mid - 1;
  }
  const std::uint8_t* scan = data_base + lo * granularity;
  std::uint64_t count = cumsum[lo];
  while (count < target && scan < data_end) {
    if (!(*scan & 0x80)) {
      ++scan;
      ++count;
    } else {
      do {
        ++scan;
      } while (scan < data_end && (*scan & 0x80));
      if (scan >= data_end) break;
      ++scan;
      ++count;
    }
  }
  return {scan, count};
}

[[nodiscard]] std::pair<std::vector<std::uint64_t>, std::size_t>
BuildVarintCumulativeCountParallel(std::span<const std::byte> data) {
  const auto n = data.size();
  constexpr std::size_t kGranularity = 4096;

  constexpr std::size_t kMinPartBlocks = 256;
  const std::size_t hw_threads = std::thread::hardware_concurrency();
  const std::size_t num_parts = std::min(n / (kGranularity * kMinPartBlocks),
                                         std::max<std::size_t>(1, hw_threads));

  if (num_parts <= 1) {
    return BuildVarintCumulativeCount(data);
  }

  const std::size_t total_blocks = n / kGranularity;
  const std::size_t blocks_per_part = total_blocks / num_parts;
  const std::size_t part_size = blocks_per_part * kGranularity;

  struct Part {
    std::size_t byte_offset;
    std::size_t byte_count;
    std::vector<std::uint64_t> cumsum;
    std::size_t granularity;
  };
  std::vector<Part> parts(num_parts);

  for (std::size_t t = 0; t < num_parts; ++t) {
    parts[t].byte_offset = t * part_size;
    parts[t].byte_count = (t == num_parts - 1) ? n - t * part_size : part_size;
  }

  tbb::parallel_for(
      tbb::blocked_range<std::size_t>(0, num_parts),
      [&](const tbb::blocked_range<std::size_t>& range) {
        for (std::size_t t = range.begin(); t < range.end(); ++t) {
          auto [cs, gran] = BuildVarintCumulativeCount(
              data.subspan(parts[t].byte_offset, parts[t].byte_count));
          parts[t].cumsum = std::move(cs);
          parts[t].granularity = gran;
        }
      });

  std::uint64_t offset = 0;
  for (std::size_t t = 0; t < num_parts; ++t) {
    for (auto& v : parts[t].cumsum) v += offset;
    offset = parts[t].cumsum.back();
  }

  std::vector<std::uint64_t> cumsum;
  cumsum.push_back(0);
  for (std::size_t t = 0; t < num_parts; ++t) {
    const auto& part_cs = parts[t].cumsum;
    std::size_t start = 1;
    std::size_t end =
        (t == num_parts - 1) ? part_cs.size() : part_cs.size() - 1;
    for (std::size_t i = start; i < end; ++i) {
      cumsum.push_back(part_cs[i]);
    }
  }

  return {std::move(cumsum), kGranularity};
}

}  // anonymous namespace

[[nodiscard]] ParseResult<NoInitVector<std::uint16_t>> DecodeVarintScalar(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  if (palette_size == 0) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::PaletteIndexOutOfRange, "Palette", 0});
  }
  NoInitVector<std::uint16_t> out(static_cast<std::size_t>(volume),
                                  memory::NoInitAllocator<std::uint16_t>{&arena});
  const auto* p = reinterpret_cast<const std::uint8_t*>(data.data());
  const auto* end = p + data.size();
  for (std::uint64_t i = 0; i < volume; ++i) {
    auto result = DecodeOneVarint(p, end);
    if (!result) [[unlikely]] {
      return std::unexpected(result.error());
    }
    const auto idx = static_cast<std::size_t>(*result);
    if (idx >= palette_size) [[unlikely]] {
      return std::unexpected(
          ParseError{ParseError::Code::PaletteIndexOutOfRange, "BlockData", 0});
    }
    out[static_cast<std::size_t>(i)] = static_cast<std::uint16_t>(idx);
  }
  return out;
}

[[nodiscard]] ParseResult<NoInitVector<std::uint16_t>> DecodeVarintParallel(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  const auto n = static_cast<std::size_t>(volume);

  constexpr std::size_t kMinVolumeForParallel = 1 << 20;
  if (n < kMinVolumeForParallel) {
    return DecodeVarintScalar(data, volume, palette_size, arena);
  }

  NoInitVector<std::uint16_t> out;
  std::pair<std::vector<std::uint64_t>, std::size_t> cumsum_result{};

  tbb::parallel_invoke(
      [&] {
        out = NoInitVector<std::uint16_t>(
            n, memory::NoInitAllocator<std::uint16_t>{&arena});
      },
      [&] { cumsum_result = BuildVarintCumulativeCountParallel(data); });

  auto& [cumsum, granularity] = cumsum_result;

  if (cumsum.back() != volume) {
    return DecodeVarintScalar(data, volume, palette_size, arena);
  }

  const std::size_t hw_threads = std::thread::hardware_concurrency();
  const std::size_t num_chunks =
      std::min(static_cast<std::size_t>(n / 65536),
               std::max<std::size_t>(1, hw_threads * 4));

  if (num_chunks <= 1) {
    return DecodeVarintScalar(data, volume, palette_size, arena);
  }

  // SoA layout: avoids exposing a Chunk struct into the per-target TU.
  std::vector<const std::uint8_t*> chunk_ptrs(num_chunks);
  std::vector<std::size_t> chunk_offsets(num_chunks);
  std::vector<std::size_t> chunk_counts(num_chunks);

  const std::size_t blocks_per_chunk = n / num_chunks;
  const auto* base = reinterpret_cast<const std::uint8_t*>(data.data());
  const auto* end = base + data.size();

  for (std::size_t t = 0; t < num_chunks; ++t) {
    std::uint64_t target = static_cast<std::uint64_t>(t) * blocks_per_chunk;
    if (t > 0) {
      target = (target + 15) & ~15ULL;
    }
    auto [ptr, count] =
        FindVarintBoundary(base, end, cumsum, granularity, target);
    chunk_ptrs[t] = ptr;
    chunk_offsets[t] = static_cast<std::size_t>(count);
  }

  for (std::size_t t = 0; t < num_chunks - 1; ++t) {
    chunk_counts[t] = chunk_offsets[t + 1] - chunk_offsets[t];
  }
  chunk_counts[num_chunks - 1] = n - chunk_offsets[num_chunks - 1];

  std::atomic<std::uint32_t> error_flag{0};

  // Delegate the per-chunk SIMD decode to the dispatched kernel
  // implemented in simd_block_data.cc.
  DecodeVarintChunks(chunk_ptrs.data(),
                     chunk_offsets.data(),
                     chunk_counts.data(),
                     num_chunks,
                     out.data(),
                     end,
                     palette_size,
                     error_flag);

  const std::uint32_t err = error_flag.load(std::memory_order_relaxed);
  if (err != 0) [[unlikely]] {
    return std::unexpected(
        ParseError{err == 1 ? ParseError::Code::Truncated
                            : ParseError::Code::PaletteIndexOutOfRange,
                   "BlockData",
                   0});
  }

  return out;
}

[[nodiscard]] ParseResult<NoInitVector<std::uint16_t>> DecodeVarintArray(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  if (volume == 0) [[unlikely]] {
    return NoInitVector<std::uint16_t>{};
  }
  if (data.size() < volume) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::BlockDataTooSmall, "BlockData", 0});
  }
  if (data.size() == volume) [[likely]] {
    auto result = DecodeSingleByteFast(data, volume, palette_size, arena);
    if (result) {
      return result;
    }
    if (result.error().code != ParseError::Code::VarintOverflow) {
      return std::unexpected(result.error());
    }
  }
  return DecodeVarintParallel(data, volume, palette_size, arena);
}

}  // namespace fschema::schem::internal