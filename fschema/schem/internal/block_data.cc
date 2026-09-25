#include "fschema/schem/internal/block_data.h"

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_invoke.h>

#include <algorithm>
#include <atomic>
#include <bit>
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

}  // anonymous namespace

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>> DecodeVarintParallel(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  const auto n = static_cast<std::size_t>(volume);

  constexpr std::size_t kMinVolumeForParallel = 1 << 14;
  if (n < kMinVolumeForParallel) {
    return DecodeVarintScalar(data, volume, palette_size, arena);
  }

  const std::size_t hw_threads = std::thread::hardware_concurrency();
  const std::size_t num_chunks =
      std::min(static_cast<std::size_t>(n / 4096),
               std::max<std::size_t>(1, hw_threads * 4));

  if (num_chunks <= 1) {
    return DecodeVarintScalar(data, volume, palette_size, arena);
  }

  auto boundaries = BuildChunkBoundaries(data, n, num_chunks);

  std::uint64_t total_est = 0;
  for (auto c : boundaries.counts) total_est += c;
  if (total_est != n) {
    return DecodeVarintScalar(data, volume, palette_size, arena);
  }

  memory::NoInitVector<std::uint16_t> out(n, &arena);

  std::atomic<std::uint32_t> error_flag{0};

  DecodeVarintChunks(
      boundaries.ptrs.data(),
      boundaries.offsets.data(),
      boundaries.counts.data(),
      num_chunks,
      out.data(),
      reinterpret_cast<const std::uint8_t*>(data.data()) + data.size(),
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

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>> DecodeVarintScalar(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  if (palette_size == 0) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::PaletteIndexOutOfRange, "Palette", 0});
  }
  memory::NoInitVector<std::uint16_t> out(static_cast<std::size_t>(volume), &arena);
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

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>> DecodeVarintArray(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  if (volume == 0) [[unlikely]] {
    return memory::NoInitVector<std::uint16_t>{};
  }
  if (data.size() < volume) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::BlockDataTooSmall, "BlockData", 0});
  }

  if (palette_size <= 128 && data.size() >= volume) [[likely]] {
    auto result = DecodeSingleByteFast(data, volume, palette_size, arena);
    if (result) {
      return result;
    }
    if (result.error().code != ParseError::Code::VarintOverflow) {
      return std::unexpected(result.error());
    }
  }

  if (palette_size > 128 && palette_size <= 16384 && data.size() >= 2 * volume)
      [[likely]] {
    auto result = Decode2ByteUniformFast(data, volume, palette_size, arena);
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