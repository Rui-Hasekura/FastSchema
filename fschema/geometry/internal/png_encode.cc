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

#include "fschema/geometry/internal/png_encode.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace fschema::geometry::internal {

namespace {

struct Crc32Table {
  std::array<std::uint32_t, 256> data{};
  constexpr Crc32Table() {
    for (std::uint32_t n = 0; n < 256; ++n) {
      std::uint32_t c = n;
      for (int k = 0; k < 8; ++k) {
        c = (c & 1) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
      }
      data[n] = c;
    }
  }
};

constexpr Crc32Table kCrc32Table{};

std::uint32_t Crc32(const std::uint8_t* data, std::size_t len) {
  std::uint32_t crc = 0xFFFFFFFFu;
  for (std::size_t i = 0; i < len; ++i) {
    crc = kCrc32Table.data[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
  }
  return ~crc;
}

std::uint32_t Adler32(const std::uint8_t* data, std::size_t len) {
  std::uint32_t a = 1, b = 0;
  for (std::size_t i = 0; i < len; ++i) {
    a = (a + data[i]) % 65521;
    b = (b + a) % 65521;
  }
  return (b << 16) | a;
}

void AppendBE32(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void AppendLE16(std::vector<std::uint8_t>& out, std::uint16_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void AppendChunk(std::vector<std::uint8_t>& out,
                 const char type[4],
                 const std::uint8_t* data,
                 std::size_t len) {
  AppendBE32(out, static_cast<std::uint32_t>(len));
  const std::size_t type_start = out.size();
  out.insert(out.end(), type, type + 4);
  if (len > 0) out.insert(out.end(), data, data + len);
  const std::uint32_t crc = Crc32(out.data() + type_start, 4 + len);
  AppendBE32(out, crc);
}

// Appends the zlib-wrapped deflate stream (stored blocks only) over
// `raw`. Handles arbitrary lengths by splitting into blocks of up to
// 65535 bytes each; only the last block sets BFINAL.
void AppendStoredDeflate(std::vector<std::uint8_t>& out,
                         const std::uint8_t* raw,
                         std::size_t len) {
  out.push_back(0x78);  // CMF: deflate, 32K window
  out.push_back(0x01);  // FLG: FCHECK such that (CMF*256 + FLG) % 31 == 0
  std::size_t off = 0;
  while (off < len) {
    const std::size_t block = std::min<std::size_t>(len - off, 65535u);
    const bool final_block = (off + block == len);
    out.push_back(final_block ? 0x01 : 0x00);  // BFINAL + BTYPE=00 (stored)
    AppendLE16(out, static_cast<std::uint16_t>(block));
    AppendLE16(out, static_cast<std::uint16_t>(~block & 0xFFFF));
    out.insert(out.end(), raw + off, raw + off + block);
    off += block;
  }
  AppendBE32(out, Adler32(raw, len));
}

}  // namespace

std::vector<std::uint8_t> EncodePng(std::uint32_t width,
                                    std::uint32_t height,
                                    std::span<const std::uint8_t> rgba) {
  const std::size_t scanline_size = 1u + static_cast<std::size_t>(width) * 4u;
  const std::size_t raw_size = scanline_size * height;

  std::vector<std::uint8_t> out;
  out.reserve(8 + 25 + 12 + 10 + raw_size + 12 + 16);

  constexpr std::uint8_t kSig[8] = {
      0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A};
  out.insert(out.end(), kSig, kSig + 8);

  std::uint8_t ihdr[13];
  ihdr[0] = static_cast<std::uint8_t>((width >> 24) & 0xFF);
  ihdr[1] = static_cast<std::uint8_t>((width >> 16) & 0xFF);
  ihdr[2] = static_cast<std::uint8_t>((width >> 8) & 0xFF);
  ihdr[3] = static_cast<std::uint8_t>(width & 0xFF);
  ihdr[4] = static_cast<std::uint8_t>((height >> 24) & 0xFF);
  ihdr[5] = static_cast<std::uint8_t>((height >> 16) & 0xFF);
  ihdr[6] = static_cast<std::uint8_t>((height >> 8) & 0xFF);
  ihdr[7] = static_cast<std::uint8_t>(height & 0xFF);
  ihdr[8] = 8;   // bit depth
  ihdr[9] = 6;   // color type: RGBA
  ihdr[10] = 0;  // compression
  ihdr[11] = 0;  // filter
  ihdr[12] = 0;  // interlace
  AppendChunk(out, "IHDR", ihdr, sizeof(ihdr));

  std::vector<std::uint8_t> raw(raw_size);
  std::uint8_t* dst = raw.data();
  for (std::uint32_t y = 0; y < height; ++y) {
    *dst++ = 0;  // filter = None
    std::memcpy(dst, rgba.data() + y * width * 4, width * 4);
    dst += width * 4;
  }

  std::vector<std::uint8_t> idat;
  idat.reserve(2 + (raw_size / 65535 + 1) * 5 + raw_size + 4);
  AppendStoredDeflate(idat, raw.data(), raw.size());
  AppendChunk(out, "IDAT", idat.data(), idat.size());

  AppendChunk(out, "IEND", nullptr, 0);

  return out;
}

}  // namespace fschema::geometry::internal
