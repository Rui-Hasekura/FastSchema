/*
 * Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License,
 * Version 2.0 (the "License");
 * you may not use this file except in
 * compliance with the License.
 * You may obtain a copy of the License at
 *
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by
 * applicable law or agreed to in writing, software
 * distributed under the
 * License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR
 * CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the
 * specific language governing permissions and
 * limitations under the
 * License.
 */

#ifndef FSCHEMA_IR_INTERNAL_CODEC_UTILS_H_
#define FSCHEMA_IR_INTERNAL_CODEC_UTILS_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "fschema/base/limits.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_writer.h"

namespace fschema::ir::internal {

// Reorder indices between Litematica (YZX) and Sponge (XZY) layouts.
inline void TransposeYzxToXzy(std::span<const std::uint16_t> src,
                              std::span<std::uint16_t> dst,
                              int W,
                              int H,
                              int L) {
  for (int y = 0; y < H; ++y) {
    for (int z = 0; z < L; ++z) {
      for (int x = 0; x < W; ++x) {
        std::uint64_t yzx_idx = static_cast<std::uint64_t>(y) *
                                    (static_cast<std::uint64_t>(W) * L) +
                                static_cast<std::uint64_t>(z) * W + x;
        std::uint64_t xzy_idx =
            static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(z) * W +
            static_cast<std::uint64_t>(y) * (static_cast<std::uint64_t>(W) * L);
        dst[xzy_idx] = src[yzx_idx];
      }
    }
  }
}

inline void TransposeXzyToYzx(std::span<const std::uint16_t> src,
                              std::span<std::uint16_t> dst,
                              int W,
                              int H,
                              int L) {
  for (int y = 0; y < H; ++y) {
    for (int z = 0; z < L; ++z) {
      for (int x = 0; x < W; ++x) {
        std::uint64_t xzy_idx =
            static_cast<std::uint64_t>(x) + static_cast<std::uint64_t>(z) * W +
            static_cast<std::uint64_t>(y) * (static_cast<std::uint64_t>(W) * L);
        std::uint64_t yzx_idx = static_cast<std::uint64_t>(y) *
                                    (static_cast<std::uint64_t>(W) * L) +
                                static_cast<std::uint64_t>(z) * W + x;
        dst[yzx_idx] = src[xzy_idx];
      }
    }
  }
}

inline constexpr std::string_view kTeSkip[] =
    {"Id", "id", "Pos", "x", "y", "z"};
inline constexpr std::string_view kBeSkip[] =
    {"Id", "id", "Pos", "x", "y", "z"};
inline constexpr std::string_view kEntSkip[] = {"Id",
                                                "id",
                                                "Pos",
                                                "Motion",
                                                "Rotation"};

// NBT Compound (raw bytes) -> "k=v,k=v" string
[[nodiscard]] inline std::string NbtPropsToString(
    std::span<const std::byte> nbt,
    const fschema::base::DecodeLimits& limits = {}) {
  std::string out;
  if (nbt.empty()) return out;
  fschema::base::ByteReader reader(nbt, limits);
  reader.push_depth();
  for (;;) {
    std::string_view key;
    auto tag_result = reader.ReadCompoundEntryHeaderView(key);
    if (!tag_result) break;
    if (*tag_result == fschema::base::TagType::End) break;
    if (*tag_result != fschema::base::TagType::String) {
      (void)fschema::base::SkipPayload(reader, *tag_result);
      continue;
    }
    auto value = reader.ReadStringView();
    if (!value) break;
    if (!out.empty()) out += ",";
    out += key;
    out += "=";
    out += *value;
  }
  reader.pop_depth();
  return out;
}

[[nodiscard]] ParseResult<void> FilterAndWriteFields(
    std::span<const std::byte> raw_nbt,
    std::span<const std::string_view> skip_names,
    fschema::base::NbtWriter& writer);

[[nodiscard]] ParseResult<void> FilterAndWriteTileEntityFields(
    std::span<const std::byte> raw_nbt,
    std::span<const std::string_view> skip_names,
    fschema::base::NbtWriter& writer);

}  // namespace fschema::ir::internal

#endif  // FSCHEMA_IR_INTERNAL_CODEC_UTILS_H_