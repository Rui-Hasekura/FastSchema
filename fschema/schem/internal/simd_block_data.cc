#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "fschema/schem/internal/simd_block_data.cc"

#include <atomic>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <utility>
#include <vector>

#include "hwy/foreach_target.h"
#include "hwy/highway.h"

#if defined(_MSC_VER) || defined(__x86_64__) || defined(__i386__)
#  include <immintrin.h>
#endif

#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

#include "fschema/base/error.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/noinit_allocator.h"
#include "fschema/schem/internal/block_data.h"
#include "fschema/schem/types.h"

HWY_BEFORE_NAMESPACE();
namespace fschema::schem::internal {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

[[nodiscard]] ChunkBoundary BuildChunkBoundariesImpl(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t num_chunks) {
  const auto* base = reinterpret_cast<const std::uint8_t*>(data.data());
  const auto* end = base + data.size();

  std::vector<std::size_t> byte_starts(num_chunks + 1);
  for (std::size_t t = 0; t <= num_chunks; ++t) {
    byte_starts[t] = (t * (end - base)) / num_chunks;
  }

  std::vector<const std::uint8_t*> bounds(num_chunks + 1);
  bounds[0] = base;
  bounds[num_chunks] = end;

  tbb::parallel_for(
      tbb::blocked_range<std::size_t>(0, num_chunks - 1),
      [&](const tbb::blocked_range<std::size_t>& range) {
        for (std::size_t t = range.begin(); t < range.end(); ++t) {
          const std::uint8_t* p = base + byte_starts[t + 1];
          while (p > base && (*(p - 1) & 0x80)) {
            --p;
          }
          bounds[t + 1] = p;
        }
      },
      tbb::static_partitioner{});

  std::vector<std::uint64_t> counts(num_chunks);
  tbb::parallel_for(
      tbb::blocked_range<std::size_t>(0, num_chunks),
      [&](const tbb::blocked_range<std::size_t>& range) {
        for (std::size_t t = range.begin(); t < range.end(); ++t) {
          std::uint64_t count = 0;
          const std::uint8_t* p = bounds[t];
          const std::uint8_t* chunk_end = bounds[t + 1];

#if HWY_TARGET == HWY_AVX2
          const __m256i mask = _mm256_set1_epi8(static_cast<char>(0x80));
          const __m256i zero = _mm256_setzero_si256();
          for (; p + 32 <= chunk_end; p += 32) {
            __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
            __m256i is_end = _mm256_cmpeq_epi8(_mm256_and_si256(v, mask), zero);
            count += std::popcount(
                static_cast<std::uint32_t>(_mm256_movemask_epi8(is_end)));
          }
#else
          const hn::ScalableTag<std::uint8_t> d8;
          const auto v_msb = hn::Set(d8, static_cast<std::uint8_t>(0x80));
          const auto v_zero = hn::Zero(d8);
          const std::size_t lanes = hn::Lanes(d8);
          for (; p + lanes <= chunk_end; p += lanes) {
            auto v = hn::LoadU(d8, p);
            auto is_end = hn::Eq(hn::And(v, v_msb), v_zero);
            count += hn::CountTrue(d8, is_end);
          }
#endif
          for (; p < chunk_end; ++p) {
            if ((*p & 0x80) == 0) ++count;
          }
          counts[t] = count;
        }
      },
      tbb::static_partitioner{});

  std::vector<std::size_t> offsets(num_chunks);
  std::uint64_t current_offset = 0;
  for (std::size_t t = 0; t < num_chunks; ++t) {
    offsets[t] = current_offset;
    current_offset += counts[t];
  }

  ChunkBoundary cb;
  cb.ptrs.assign(bounds.begin(), bounds.end() - 1);
  cb.offsets = std::move(offsets);
  cb.counts.assign(counts.begin(), counts.end());
  return cb;
}

#if HWY_TARGET == HWY_AVX2

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>> DecodeSingleByteFastImpl(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  const auto n = static_cast<std::size_t>(volume);
  const auto* src = reinterpret_cast<const std::uint8_t*>(data.data());

  memory::NoInitVector<std::uint16_t> out(n, &arena);
  auto* dst = out.data();

  std::atomic<std::uint32_t> error_flag{0};

  std::size_t i = 0;
  while ((reinterpret_cast<std::uintptr_t>(src + i) & 31) != 0 && i < n) {
    if (src[i] & 0x80) {
      error_flag.store(1, std::memory_order_relaxed);
    }
    dst[i] = static_cast<std::uint16_t>(src[i]);
    ++i;
  }

  const std::size_t n32 = ((n - i) / 32) * 32 + i;
  const auto* s = src + i;
  auto* d = dst + i;

  const __m256i v_msb_mask = _mm256_set1_epi8(static_cast<char>(0x80));
  const __m256i v_zero = _mm256_setzero_si256();

  for (; s < src + n32; s += 32, d += 32) {
    __m256i v32 = _mm256_load_si256(reinterpret_cast<const __m256i*>(s));

    __m128i lo = _mm256_castsi256_si128(v32);
    __m128i hi = _mm256_extracti128_si256(v32, 1);
    __m256i wide0 = _mm256_cvtepu8_epi16(lo);
    __m256i wide1 = _mm256_cvtepu8_epi16(hi);

    _mm256_stream_si256(reinterpret_cast<__m256i*>(d), wide0);
    _mm256_stream_si256(reinterpret_cast<__m256i*>(d + 16), wide1);

    __m256i msb = _mm256_and_si256(v32, v_msb_mask);
    if (!_mm256_testz_si256(msb, msb)) {
      std::uint32_t expected = 0;
      error_flag.compare_exchange_strong(
          expected, 1, std::memory_order_relaxed);
    }
  }

  _mm_sfence();

  for (std::size_t j = n32; j < n; ++j) {
    if (src[j] & 0x80) {
      error_flag.store(1, std::memory_order_relaxed);
    }
    dst[j] = static_cast<std::uint16_t>(src[j]);
  }

  std::uint32_t err = error_flag.load(std::memory_order_relaxed);
  if (err != 0) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::VarintOverflow, "BlockData", 0});
  }

  return out;
}

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>>
Decode2ByteUniformFastImpl(std::span<const std::byte> data,
                           std::uint64_t volume,
                           std::size_t palette_size,
                           memory::Arena& arena) {
  const auto n = static_cast<std::size_t>(volume);
  const auto* src = reinterpret_cast<const std::uint8_t*>(data.data());

  memory::NoInitVector<std::uint16_t> out(n, &arena);
  auto* dst = out.data();

  std::atomic<std::uint32_t> error_flag{0};

  const __m256i mask_8080 = _mm256_set1_epi16(static_cast<short>(0x8080));
  const __m256i expected_0080 = _mm256_set1_epi16(static_cast<short>(0x0080));
  const __m256i mask_lo = _mm256_set1_epi16(0x007F);
  const __m256i mask_hi = _mm256_set1_epi16(0x7F00);

  std::size_t i = 0;
  while ((reinterpret_cast<std::uintptr_t>(src + i * 2) & 31) != 0 && i < n) {
    std::uint8_t b0 = src[i * 2];
    std::uint8_t b1 = src[i * 2 + 1];
    if ((b0 & 0x80) == 0 || (b1 & 0x80) != 0) {
      error_flag.store(1, std::memory_order_relaxed);
    }
    dst[i] = ((b1 & 0x7F) << 7) | (b0 & 0x7F);
    ++i;
  }

  const std::size_t n16 = ((n - i) / 16) * 16 + i;
  const auto* s = src + i * 2;
  auto* d = dst + i;

  for (; s < src + n16 * 2; s += 32, d += 16) {
    __m256i v = _mm256_load_si256(reinterpret_cast<const __m256i*>(s));

    __m256i msb = _mm256_and_si256(v, mask_8080);
    __m256i is_bad = _mm256_xor_si256(msb, expected_0080);
    if (!_mm256_testz_si256(is_bad, is_bad)) {
      std::uint32_t expected = 0;
      error_flag.compare_exchange_strong(
          expected, 1, std::memory_order_relaxed);
    }

    __m256i lo = _mm256_and_si256(v, mask_lo);
    __m256i hi = _mm256_and_si256(v, mask_hi);
    __m256i res = _mm256_or_si256(lo, _mm256_srli_epi16(hi, 1));

    _mm256_stream_si256(reinterpret_cast<__m256i*>(d), res);
  }

  _mm_sfence();

  for (std::size_t j = n16; j < n; ++j) {
    std::uint8_t b0 = src[j * 2];
    std::uint8_t b1 = src[j * 2 + 1];
    if ((b0 & 0x80) == 0 || (b1 & 0x80) != 0) {
      error_flag.store(1, std::memory_order_relaxed);
    }
    dst[j] = ((b1 & 0x7F) << 7) | (b0 & 0x7F);
  }

  std::uint32_t err = error_flag.load(std::memory_order_relaxed);
  if (err != 0) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::VarintOverflow, "BlockData", 0});
  }

  return out;
}

#else  // Generic Highway path

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>> DecodeSingleByteFastImpl(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  const auto n = static_cast<std::size_t>(volume);
  const auto* src = reinterpret_cast<const std::uint8_t*>(data.data());

  memory::NoInitVector<std::uint16_t> out(n, &arena);
  auto* dst = out.data();

  const hn::ScalableTag<std::uint8_t> d8;
  const hn::Repartition<std::uint16_t, decltype(d8)> d16;
  const std::size_t lanes8 = hn::Lanes(d8);
  const std::size_t lanes16 = hn::Lanes(d16);

  const auto v_msb_mask = hn::Set(d8, static_cast<std::uint8_t>(0x80));
  auto v_msb_acc = hn::Zero(d8);

  const std::size_t n_simd = (n / lanes8) * lanes8;
  std::size_t i = 0;

  for (; i < n_simd; i += lanes8) {
    const auto v = hn::LoadU(d8, src + i);
    const auto lo16 = hn::PromoteLowerTo(d16, v);
    const auto hi16 = hn::PromoteUpperTo(d16, v);
    hn::StoreU(lo16, d16, dst + i);
    hn::StoreU(hi16, d16, dst + i + lanes16);

    v_msb_acc = hn::Or(v_msb_acc, hn::And(v, v_msb_mask));
  }

  bool format_error = !hn::AllTrue(d8, hn::Eq(v_msb_acc, hn::Zero(d8)));

  for (; i < n; ++i) {
    if (src[i] & 0x80) format_error = true;
    dst[i] = static_cast<std::uint16_t>(src[i]);
  }

  if (format_error) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::VarintOverflow, "BlockData", 0});
  }

  return out;
}

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>>
Decode2ByteUniformFastImpl(std::span<const std::byte> data,
                           std::uint64_t volume,
                           std::size_t palette_size,
                           memory::Arena& arena) {
  const auto n = static_cast<std::size_t>(volume);
  const auto* src = reinterpret_cast<const std::uint8_t*>(data.data());

  memory::NoInitVector<std::uint16_t> out(n, &arena);
  auto* dst = out.data();

  const hn::ScalableTag<std::uint16_t> d16;
  const std::size_t lanes = hn::Lanes(d16);

  const auto v_mask_8080 = hn::Set(d16, static_cast<std::uint16_t>(0x8080));
  const auto v_exp_0080 = hn::Set(d16, static_cast<std::uint16_t>(0x0080));
  const auto v_mask_lo = hn::Set(d16, static_cast<std::uint16_t>(0x007F));
  const auto v_mask_hi = hn::Set(d16, static_cast<std::uint16_t>(0x7F00));

  const std::size_t n_simd = (n / lanes) * lanes;
  std::size_t i = 0;
  bool format_error = false;

  for (; i < n_simd; i += lanes) {
    auto v =
        hn::LoadU(d16, reinterpret_cast<const std::uint16_t*>(src + i * 2));

    auto msb = hn::And(v, v_mask_8080);
    if (!hn::AllTrue(d16, hn::Eq(msb, v_exp_0080))) {
      format_error = true;
    }

    auto lo = hn::And(v, v_mask_lo);
    auto hi = hn::And(v, v_mask_hi);
    auto res = hn::Or(lo, hn::ShiftRight<1>(hi));

    hn::StoreU(res, d16, dst + i);
  }

  for (; i < n; ++i) {
    std::uint8_t b0 = src[i * 2];
    std::uint8_t b1 = src[i * 2 + 1];
    if ((b0 & 0x80) == 0 || (b1 & 0x80) != 0) {
      format_error = true;
    }
    dst[i] = ((b1 & 0x7F) << 7) | (b0 & 0x7F);
  }

  if (format_error) [[unlikely]] {
    return std::unexpected(
        ParseError{ParseError::Code::VarintOverflow, "BlockData", 0});
  }

  return out;
}

#endif  // HWY_TARGET == HWY_AVX2

void DecodeVarintChunksKernelImpl(const std::uint8_t* const* chunk_ptrs,
                                  const std::size_t* chunk_offsets,
                                  const std::size_t* chunk_counts,
                                  std::size_t num_chunks,
                                  std::uint16_t* out,
                                  const std::uint8_t* data_end,
                                  std::size_t palette_size,
                                  std::atomic<std::uint32_t>& error_flag) {
  tbb::parallel_for(
      tbb::blocked_range<std::size_t>(0, num_chunks),
      [&](const tbb::blocked_range<std::size_t>& range) {
        for (std::size_t t = range.begin(); t < range.end(); ++t) {
          const std::uint8_t* p = chunk_ptrs[t];
          std::size_t target_block = chunk_offsets[t];
          std::size_t cnt = chunk_counts[t];
          std::uint16_t* dst = out + target_block;

          if (palette_size <= 16384) {
#if HWY_TARGET == HWY_AVX2
            std::size_t i = 0;

            // 1. 对齐 dst 到 32 字节边界
            while ((reinterpret_cast<std::uintptr_t>(dst + i) & 31) != 0 &&
                   i < cnt) {
              if (p >= data_end) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 1, std::memory_order_relaxed);
                return;
              }
              std::uint8_t b0 = *p++;
              std::uint16_t val = b0;
              if (b0 & 0x80) {
                if (p >= data_end) [[unlikely]] {
                  std::uint32_t expected = 0;
                  error_flag.compare_exchange_strong(
                      expected, 1, std::memory_order_relaxed);
                  return;
                }
                val = (b0 & 0x7F) | (static_cast<std::uint16_t>(*p++) << 7);
              }
              if (val >= palette_size) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 2, std::memory_order_relaxed);
                return;
              }
              if (val != 0) [[likely]] {
                dst[i] = val;
              }
              ++i;
            }

            // 2. SIMD 循环
            for (; i + 32 <= cnt && p + 64 <= data_end;) {
              __m256i v0 =
                  _mm256_loadu_si256(reinterpret_cast<const __m256i*>(p));
              bool is_all_zero = _mm256_testz_si256(v0, v0);

              if (is_all_zero) {
                p += 32;
                i += 32;
              } else {
                alignas(32) std::uint16_t buf16[32];
                std::size_t decoded = 0;
                while (decoded < 32) {
                  std::uint8_t b0 = *p;
                  std::uint16_t val;
                  if (!(b0 & 0x80)) {
                    val = b0;
                    p += 1;
                  } else {
                    val = (b0 & 0x7F) | (static_cast<std::uint16_t>(p[1]) << 7);
                    p += 2;
                  }
                  buf16[decoded++] = val;
                }
                __m256i v1 =
                    _mm256_load_si256(reinterpret_cast<const __m256i*>(buf16));
                __m256i v2 = _mm256_load_si256(
                    reinterpret_cast<const __m256i*>(buf16 + 16));
                _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + i), v1);
                _mm256_stream_si256(reinterpret_cast<__m256i*>(dst + i + 16),
                                    v2);
                i += 32;
              }
            }

            // 3. 标量尾部
            for (; i < cnt; ++i) {
              if (p >= data_end) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 1, std::memory_order_relaxed);
                return;
              }
              std::uint8_t b0 = *p++;
              std::uint16_t val = b0;
              if (b0 & 0x80) {
                if (p >= data_end) [[unlikely]] {
                  std::uint32_t expected = 0;
                  error_flag.compare_exchange_strong(
                      expected, 1, std::memory_order_relaxed);
                  return;
                }
                val = (b0 & 0x7F) | (static_cast<std::uint16_t>(*p++) << 7);
              }
              if (val >= palette_size) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 2, std::memory_order_relaxed);
                return;
              }
              if (val != 0) [[likely]] {
                dst[i] = val;
              }
            }
#else
            const hn::ScalableTag<std::uint8_t> d8;
            const auto v_zero = hn::Zero(d8);
            std::size_t i = 0;

            for (; i + 32 <= cnt && p + 64 <= data_end;) {
              auto v0 = hn::LoadU(d8, p);
              bool is_all_zero = hn::AllTrue(d8, hn::Eq(v0, v_zero));

              if (is_all_zero) {
                p += 32;
                i += 32;
              } else {
                std::size_t decoded = 0;
                while (decoded < 32) {
                  std::uint8_t b0 = *p;
                  std::uint16_t val;
                  if (!(b0 & 0x80)) {
                    val = b0;
                    p += 1;
                  } else {
                    val = (b0 & 0x7F) | (static_cast<std::uint16_t>(p[1]) << 7);
                    p += 2;
                  }
                  dst[i + decoded] = val;
                  decoded++;
                }
                i += 32;
              }
            }

            for (; i < cnt; ++i) {
              if (p >= data_end) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 1, std::memory_order_relaxed);
                return;
              }
              std::uint8_t b0 = *p++;
              std::uint16_t val = b0;
              if (b0 & 0x80) {
                if (p >= data_end) [[unlikely]] {
                  std::uint32_t expected = 0;
                  error_flag.compare_exchange_strong(
                      expected, 1, std::memory_order_relaxed);
                  return;
                }
                val = (b0 & 0x7F) | (static_cast<std::uint16_t>(*p++) << 7);
              }
              if (val >= palette_size) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 2, std::memory_order_relaxed);
                return;
              }
              if (val != 0) [[likely]] {
                dst[i] = val;
              }
            }
#endif
          } else {
            for (std::size_t i = 0; i < cnt; ++i) {
              if (p >= data_end) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 1, std::memory_order_relaxed);
                return;
              }
              std::uint32_t value = *p++;
              if (value & 0x80) {
                value &= 0x7F;
                std::uint32_t shift = 7;
                do {
                  if (p >= data_end) [[unlikely]] {
                    std::uint32_t expected = 0;
                    error_flag.compare_exchange_strong(
                        expected, 1, std::memory_order_relaxed);
                    return;
                  }
                  std::uint8_t b = *p++;
                  value |= (b & 0x7F) << shift;
                  if (!(b & 0x80)) break;
                  shift += 7;
                  if (shift > 28) [[unlikely]] {
                    std::uint32_t expected = 0;
                    error_flag.compare_exchange_strong(
                        expected, 1, std::memory_order_relaxed);
                    return;
                  }
                } while (true);
              }
              if (value >= palette_size) [[unlikely]] {
                std::uint32_t expected = 0;
                error_flag.compare_exchange_strong(
                    expected, 2, std::memory_order_relaxed);
                return;
              }
              if (value != 0) [[likely]] {
                dst[i] = static_cast<std::uint16_t>(value);
              }
            }
          }
        }
      },
      tbb::static_partitioner{});
}

}  // namespace HWY_NAMESPACE
}  // namespace fschema::schem::internal
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fschema::schem::internal {
HWY_EXPORT(BuildChunkBoundariesImpl);
HWY_EXPORT(DecodeSingleByteFastImpl);
HWY_EXPORT(Decode2ByteUniformFastImpl);
HWY_EXPORT(DecodeVarintChunksKernelImpl);

[[nodiscard]] ChunkBoundary BuildChunkBoundaries(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t num_chunks) {
  return HWY_DYNAMIC_DISPATCH(BuildChunkBoundariesImpl)(
      data, volume, num_chunks);
}

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>> DecodeSingleByteFast(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  return HWY_DYNAMIC_DISPATCH(DecodeSingleByteFastImpl)(
      data, volume, palette_size, arena);
}

[[nodiscard]] ParseResult<memory::NoInitVector<std::uint16_t>> Decode2ByteUniformFast(
    std::span<const std::byte> data,
    std::uint64_t volume,
    std::size_t palette_size,
    memory::Arena& arena) {
  return HWY_DYNAMIC_DISPATCH(Decode2ByteUniformFastImpl)(
      data, volume, palette_size, arena);
}

void DecodeVarintChunks(const std::uint8_t* const* chunk_ptrs,
                        const std::size_t* chunk_offsets,
                        const std::size_t* chunk_counts,
                        std::size_t num_chunks,
                        std::uint16_t* out,
                        const std::uint8_t* data_end,
                        std::size_t palette_size,
                        std::atomic<std::uint32_t>& error_flag) {
  HWY_DYNAMIC_DISPATCH(DecodeVarintChunksKernelImpl)(chunk_ptrs,
                                                     chunk_offsets,
                                                     chunk_counts,
                                                     num_chunks,
                                                     out,
                                                     data_end,
                                                     palette_size,
                                                     error_flag);
}

}  // namespace fschema::schem::internal
#endif  // HWY_ONCE