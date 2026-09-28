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

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "fschema/base/byte_order.cc"

#include "fschema/base/byte_order.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <type_traits>

#include "hwy/foreach_target.h"
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace fschema::base::HWY_NAMESPACE {
namespace hn = hwy::HWY_NAMESPACE;

namespace {

template <std::size_t kBytes>
void CopyAndBswapImpl(const std::byte* src, std::byte* dst, std::size_t count) {
  static_assert(kBytes == 4 || kBytes == 8,
                "Only 4- and 8-byte swaps are supported");
  using Scalar = std::conditional_t<kBytes == 4, std::int32_t, std::int64_t>;

  const hn::ScalableTag<std::uint8_t> d8;
  const std::size_t N = hn::Lanes(d8) / kBytes;

  std::size_t i = 0;
  for (; i + N <= count; i += N) {
    auto v =
        hn::LoadU(d8, reinterpret_cast<const std::uint8_t*>(src) + i * kBytes);
    if constexpr (kBytes == 4) {
      v = hn::Reverse4(d8, v);
    } else {
      v = hn::Reverse8(d8, v);
    }
    hn::StoreU(v, d8, reinterpret_cast<std::uint8_t*>(dst) + i * kBytes);
  }

  for (; i < count; ++i) {
    Scalar val;
    std::memcpy(&val, src + i * kBytes, kBytes);
    val = std::byteswap(val);
    std::memcpy(reinterpret_cast<std::byte*>(dst) + i * kBytes, &val, kBytes);
  }
}

}  // namespace

void CopyAndBswap32(const std::byte* src,
                    std::int32_t* dst,
                    std::size_t count) {
  CopyAndBswapImpl<4>(src, reinterpret_cast<std::byte*>(dst), count);
}

void CopyAndBswap64(const std::byte* src,
                    std::int64_t* dst,
                    std::size_t count) {
  CopyAndBswapImpl<8>(src, reinterpret_cast<std::byte*>(dst), count);
}

}  // namespace fschema::base::HWY_NAMESPACE
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace fschema::base {

HWY_EXPORT(CopyAndBswap32);
HWY_EXPORT(CopyAndBswap64);

void CopyAndBswap32(const std::byte* src,
                    std::int32_t* dst,
                    std::size_t count) noexcept {
  HWY_DYNAMIC_DISPATCH(CopyAndBswap32)(src, dst, count);
}

void CopyAndBswap64(const std::byte* src,
                    std::int64_t* dst,
                    std::size_t count) noexcept {
  HWY_DYNAMIC_DISPATCH(CopyAndBswap64)(src, dst, count);
}

}  // namespace fschema::base
#endif  // HWY_ONCE