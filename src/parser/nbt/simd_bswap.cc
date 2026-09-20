// Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
// SPDX-License-Identifier: Apache-2.0

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "parser/nbt/simd_bswap.cc"

#include "hwy/foreach_target.h"
#include "hwy/highway.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "parser/nbt/simd_bswap.h"

HWY_BEFORE_NAMESPACE();
namespace fschema::parser::nbt::HWY_NAMESPACE {
  namespace hn = hwy::HWY_NAMESPACE;

  void CopyAndBswap32(const std::byte* src, std::int32_t* dst, std::size_t count) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_bytes = hn::Lanes(d8);
    const size_t N_32 = N_bytes / 4;

    size_t i = 0;
    for (; i + N_32 <= count; i += N_32) {
      auto v = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src) + i * 4);
      v = hn::Reverse4(d8, v);
      hn::Stream(v, d8, reinterpret_cast<uint8_t*>(dst) + i * 4);
    }

    for (; i < count; ++i) {
      std::int32_t val;
      std::memcpy(&val, src + i * 4, 4);
      dst[i] = std::byteswap(val);
    }
  }

  void CopyAndBswap64(const std::byte* src, std::int64_t* dst, std::size_t count) {
    const hn::ScalableTag<uint8_t> d8;
    const size_t N_bytes = hn::Lanes(d8);
    const size_t N_64 = N_bytes / 8;

    size_t i = 0;
    for (; i + N_64 <= count; i += N_64) {
      auto v = hn::LoadU(d8, reinterpret_cast<const uint8_t*>(src) + i * 8);
      v = hn::Reverse8(d8, v);
      hn::Stream(v, d8, reinterpret_cast<uint8_t*>(dst) + i * 8);
    }

    for (; i < count; ++i) {
      std::int64_t val;
      std::memcpy(&val, src + i * 8, 8);
      dst[i] = std::byteswap(val);
    }
  }

} // namespace fschema::parser::nbt::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fschema::parser::nbt {

  HWY_EXPORT(CopyAndBswap32);
  HWY_EXPORT(CopyAndBswap64);

  void CopyAndBswap32(const std::byte* src, std::int32_t* dst, std::size_t count) noexcept {
    HWY_DYNAMIC_DISPATCH(CopyAndBswap32)(src, dst, count);
  }

  void CopyAndBswap64(const std::byte* src, std::int64_t* dst, std::size_t count) noexcept {
    HWY_DYNAMIC_DISPATCH(CopyAndBswap64)(src, dst, count);
  }

} // namespace fschema::parser::nbt
#endif // HWY_ONCE