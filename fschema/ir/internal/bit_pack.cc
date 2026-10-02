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

#include "fschema/ir/internal/bit_pack.h"

#include <bit>
#include <cstdint>
#include <cstring>
#include <numeric>
#include <span>

#include "fschema/base/port.h"
#include "tbb/parallel_for.h"

namespace fschema::ir::internal {

namespace {

// kBigEndian = true : emit disk BE (bswap fused into store)
// kBigEndian = false: emit host-order longs (caller bswaps later)
template <bool kBigEndian>
FSCHEMA_HOT void PackChunk(std::byte* FSCHEMA_RESTRICT out,
                           std::span<const std::uint16_t> s,
                           unsigned bpb) {
  std::uint64_t acc = 0;
  unsigned bits = 0;
  auto store = [&](std::uint64_t w) {
    if constexpr (kBigEndian) {
      const std::uint64_t be = std::byteswap(w);
      std::memcpy(out, &be, 8);
    } else {
      std::memcpy(out, &w, 8);
    }
    out += 8;
  };

  for (std::uint16_t v : s) {
    acc |= std::uint64_t{v} << bits;
    if (bits + bpb > 64) {
      store(acc);
      acc = std::uint64_t{v} >> (64 - bits);
      bits = bits + bpb - 64;
    } else {
      bits += bpb;
      if (bits == 64) {
        store(acc);
        acc = 0;
        bits = 0;
      }
    }
  }
  if (bits != 0) store(acc);
}

}  // namespace

FSCHEMA_HOT std::size_t PackIndicesLitematicInto(
    std::byte* FSCHEMA_RESTRICT out,
    std::span<const std::uint16_t> indices,
    std::uint32_t bits_per_block) {
  const std::size_t n = indices.size();
  if (n == 0 || bits_per_block == 0) return 0;
  const unsigned bpb = bits_per_block;

  const unsigned s_group = 64u / std::gcd(bpb, 64u);
  // S consecutive blocks occupy S*bpb bits, a whole number of longs
  // (S = 64 / gcd(bpb, 64)). Chunk = S * 16384 blocks -> bpb=12 gives
  // 256K blocks / 384 KiB output per chunk. Tail call below relies on
  // BODY being a multiple of S, so it starts on a long boundary.
  const std::size_t kChunk = static_cast<std::size_t>(s_group) * 16384u;
  const std::size_t longs_per_chunk = kChunk * bpb / 64;

  if (n <= kChunk) {
    PackChunk<true>(out, indices, bpb);
    return LitematicLongCount(n, bpb) * 8;
  }

  const std::size_t body = (n / kChunk) * kChunk;
  const std::size_t nchunks = body / kChunk;

  tbb::parallel_for(std::size_t{0}, nchunks, [&](std::size_t c) {
    PackChunk<true>(out + c * longs_per_chunk * 8,
                    indices.subspan(c * kChunk, kChunk),
                    bpb);
  });
  if (n > body) {
    PackChunk<true>(out + (body * bpb / 64) * 8, indices.subspan(body), bpb);
  }
  return LitematicLongCount(n, bpb) * 8;
}

}  // namespace fschema::ir::internal