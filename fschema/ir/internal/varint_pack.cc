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

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "fschema/base/port.h"
#include "tbb/parallel_for.h"

namespace fschema::ir::internal {

namespace {

constexpr std::size_t kChunk = 1u << 18;  // 256K indices per chunk

[[nodiscard]] std::size_t MaxIndexValue(std::span<const std::uint16_t> s) {
  std::uint16_t m = 0;
  for (std::uint16_t v : s) m = std::max(m, v);
  return m;
}

// Exact varint size of one range, specialized by palette width.
// Relies on the IR invariant idx < palette_size (decoder-validated):
// widths are chosen from `pal` alone, values are not re-checked here.
[[nodiscard]] std::uint64_t SizeRange(std::span<const std::uint16_t> s,
                                      std::size_t pal) noexcept {
  if (pal <= 0x80) return s.size();
  if (pal <= 0x4000) {
    std::uint64_t n = 0;
    for (std::uint16_t v : s) n += 1u + (v >= 0x80u);
    return n;
  }
  std::uint64_t n = 0;
  for (std::uint16_t v : s) {
    while (v >= 0x80) {
      ++n;
      v = static_cast<std::uint16_t>(v >> 7);
    }
    ++n;
  }
  return n;
}

// Serial pack of one range into `out` (which must have room for the range's
// upper bound, see VarintUpperBound). Returns bytes written.
FSCHEMA_HOT [[nodiscard]] std::size_t PackRange(
    std::byte* out,
    std::span<const std::uint16_t> s,
    std::size_t pal) noexcept {
  std::byte* p = out;
  if (pal <= 0x80) {
    for (std::uint16_t v : s) *p++ = static_cast<std::byte>(v);
  } else if (pal <= 0x4000) {
    for (std::uint16_t v : s) {
      if (v < 0x80) {
        *p++ = static_cast<std::byte>(v);
      } else {
        *p++ = static_cast<std::byte>(v | 0x80u);
        *p++ = static_cast<std::byte>(v >> 7);
      }
    }
  } else {
    // General path: up to 3 bytes for 16-bit values (rare, huge palettes).
    for (std::uint16_t v : s) {
      while (v >= 0x80) {
        *p++ = static_cast<std::byte>(v | 0x80u);
        v = static_cast<std::uint16_t>(v >> 7);
      }
      *p++ = static_cast<std::byte>(v);
    }
  }
  return static_cast<std::size_t>(p - out);
}

}  // namespace

FSCHEMA_HOT std::size_t PackVarintInto(std::byte* out,
                                       std::span<const std::uint16_t> indices,
                                       std::size_t palette_size) {
  const std::size_t n = indices.size();
  if (n == 0) return 0;
  const std::size_t pal =
      palette_size != 0 ? palette_size : MaxIndexValue(indices) + 1;

  if (n <= kChunk) return PackRange(out, indices, pal);

  if (pal <= 0x80) {
    const std::size_t nchunks = (n + kChunk - 1) / kChunk;
    tbb::parallel_for(std::size_t{0}, nchunks, [&](std::size_t c) {
      const std::size_t b = c * kChunk;
      PackRange(out + b, indices.subspan(b, std::min(kChunk, n - b)), pal);
    });
    return n;
  }

  const std::size_t nchunks = (n + kChunk - 1) / kChunk;
  std::vector<std::uint64_t> off(nchunks + 1, 0);
  tbb::parallel_for(std::size_t{0}, nchunks, [&](std::size_t c) {
    const std::size_t b = c * kChunk;
    off[c + 1] = SizeRange(indices.subspan(b, std::min(kChunk, n - b)), pal);
  });
  for (std::size_t c = 0; c < nchunks; ++c) off[c + 1] += off[c];
  tbb::parallel_for(std::size_t{0}, nchunks, [&](std::size_t c) {
    const std::size_t b = c * kChunk;
    PackRange(out + off[c], indices.subspan(b, std::min(kChunk, n - b)), pal);
  });
  return static_cast<std::size_t>(off[nchunks]);
}

}  // namespace fschema::ir::internal