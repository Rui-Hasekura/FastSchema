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

#ifndef FSCHEMA_IR_INTERNAL_CODEC_UTILS_H_
#define FSCHEMA_IR_INTERNAL_CODEC_UTILS_H_

#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "fschema/base/limits.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/writer.h"
#include "fschema/ir/types.h"

namespace fschema::ir::internal {

inline constexpr std::string_view kBlockEntitySkip[] =
    {"Id", "id", "Pos", "x", "y", "z"};
inline constexpr std::string_view kEntSkip[] = {"Id",
                                                "id",
                                                "Pos",
                                                "Motion",
                                                "Rotation"};

inline constexpr std::string_view kLitMetadataSkip[] = {"Name", "Author"};
inline constexpr std::string_view kLitRegionSkip[] = {"Position",
                                                      "Size",
                                                      "BlockStatePalette",
                                                      "BlockStates",
                                                      "TileEntities",
                                                      "Entities"};

inline constexpr std::string_view kSchemMetadataSkip[] = {"Name", "Author"};
inline constexpr std::string_view kSchemV2Skip[] = {"Version",
                                                    "DataVersion",
                                                    "Metadata",
                                                    "Width",
                                                    "Height",
                                                    "Length",
                                                    "Offset",
                                                    "Palette",
                                                    "PaletteMax",
                                                    "BlockData",
                                                    "BlockEntities",
                                                    "Entities"};
inline constexpr std::string_view kSchemV3Skip[] = {"Version",
                                                    "DataVersion",
                                                    "Metadata",
                                                    "Width",
                                                    "Height",
                                                    "Length",
                                                    "Offset",
                                                    "Blocks",
                                                    "Entities"};

// NBT Compound (raw bytes) -> "k=v,k=v" string
[[nodiscard]] inline std::string NbtPropsToString(
    std::span<const std::byte> nbt,
    const fschema::base::DecodeLimits& limits = {}) {
  std::string out;
  if (nbt.empty()) return out;
  out.reserve(nbt.size());
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

[[nodiscard]] inline ParseResult<void> CaptureAsExtension(
    base::ByteReader& reader,
    std::string_view name,
    base::TagType tag_type,
    std::size_t field_start,
    ir::SourceFormat source_format,
    std::vector<ir::Extension>& out) {
  auto skip_res = base::SkipPayload(reader, tag_type);
  if (!skip_res) return std::unexpected(skip_res.error());

  out.push_back(ir::Extension{name, tag_type, reader.SpanFrom(field_start)});
  return {};
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