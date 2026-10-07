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

#include "fschema/schem/internal/block_entities.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <expected>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/scope.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/base/nbt/writer.h"
#include "fschema/memory/arena.h"
#include "fschema/schem/types.h"

namespace fschema::schem::internal {

[[nodiscard]] ParseResult<BlockEntity> ParseBlockEntityCompound(
    base::ByteReader& reader,
    bool is_v3,
    memory::Arena& arena) {
  BlockEntity be;
  be.pos = {0, 0, 0};
  std::string_view id;
  std::span<const std::byte> data_payload;

  const auto start = reader.pos();

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "Id" && t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          be.id = *v;
          id = *v;
        } else if (name == "Pos" && t == base::TagType::IntArray) {
          auto len = reader.ReadLength(3);
          if (!len) return std::unexpected(len.error());
          if (*len != 3) {
            return std::unexpected(
                reader.Error(ParseError::Code::InvalidTagId));
          }
          for (int i = 0; i < 3; ++i) {
            auto v = reader.Read<std::int32_t>();
            if (!v) return std::unexpected(v.error());
            be.pos[i] = *v;
          }
        } else if (is_v3 && name == "Data" && t == base::TagType::Compound) {
          const auto data_start = reader.pos();
          auto s = base::SkipPayload(reader, base::TagType::Compound);
          if (!s) return std::unexpected(s.error());
          data_payload = reader.SpanFrom(data_start);
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  // Build be.data
  if (!is_v3) {
    be.data = reader.SpanFrom(start);
  } else {
    base::NbtWriter writer;
    writer.BeginListElementCompound();
    writer.WriteStringField("Id", id);
    writer.WriteIntArrayField("Pos",
                              std::span<const std::int32_t>(be.pos.data(), 3));
    if (!data_payload.empty()) {
      writer.WriteListElementRawPayload(data_payload);
    }
    writer.EndListElementCompound();
    std::vector<std::byte> full_payload = std::move(writer).Finalize();
    if (!full_payload.empty() && full_payload.back() == std::byte{0})
      full_payload.pop_back();
    auto* mem = arena.Allocate(full_payload.size());
    std::memcpy(mem, full_payload.data(), full_payload.size());
    be.data = std::span<const std::byte>(static_cast<const std::byte*>(mem),
                                         full_payload.size());
  }
  return be;
}

[[nodiscard]] ParseResult<void> ParseBlockEntities(
    base::ByteReader& reader,
    std::vector<BlockEntity>& out,
    bool is_v3,
    memory::Arena& arena) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;
  if (elem_type == base::TagType::End || count == 0) return {};
  if (elem_type != base::TagType::Compound)
    return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
  if (count > reader.limits().max_tile_entities)
    return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));

  out.clear();
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    auto be = ParseBlockEntityCompound(reader, is_v3, arena);
    if (!be) return std::unexpected(be.error());
    out.push_back(std::move(*be));
  }
  return {};
}

}  // namespace fschema::schem::internal