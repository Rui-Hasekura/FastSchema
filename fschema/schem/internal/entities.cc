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

#include "fschema/schem/internal/entities.h"

#include <array>
#include <cstring>
#include <expected>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt/primitives.h"
#include "fschema/base/nbt/scope.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/base/nbt/writer.h"
#include "fschema/memory/arena.h"
#include "fschema/schem/types.h"

namespace fschema::schem::internal {

[[nodiscard]] ParseResult<Entity> ParseEntityCompound(base::ByteReader& reader,
                                                      bool is_v3,
                                                      memory::Arena& arena) {
  Entity ent;
  ent.pos = {0, 0, 0};
  ent.motion = {0, 0, 0};
  ent.rotation = {0, 0};
  std::string_view id;
  std::span<const std::byte> data_payload;

  const auto start = reader.pos();

  auto result = ForEachCompoundField(
      reader, [&](std::string_view name, base::TagType t) -> ParseResult<void> {
        if (name == "Id" && t == base::TagType::String) {
          auto v = reader.ReadStringView();
          if (!v) return std::unexpected(v.error());
          ent.id = *v;
          id = *v;
        } else if (name == "Pos" && t == base::TagType::List) {
          auto r = base::ReadVec3DoubleList(reader);
          if (!r) return std::unexpected(r.error());
          ent.pos = *r;
        } else if (name == "Motion" && t == base::TagType::List) {
          auto r = base::ReadVec3DoubleList(reader);
          if (!r) return std::unexpected(r.error());
          ent.motion = *r;
        } else if (name == "Rotation" && t == base::TagType::List) {
          auto r = base::ReadVec2FloatList(reader);
          if (!r) return std::unexpected(r.error());
          ent.rotation = *r;
        } else if (is_v3 && name == "Data" && t == base::TagType::Compound) {
          const auto data_start = reader.pos();
          auto s = base::SkipPayload(reader, base::TagType::Compound);
          if (!s) return std::unexpected(s.error());
          data_payload = reader.SpanFrom(data_start);

          // Inner scan of Data compound to extract Motion/Rotation (if
          // present).
          if (!data_payload.empty()) {
            base::ByteReader data_scan(data_payload, base::DecodeLimits{});
            for (;;) {
              std::string_view dname;
              auto dtag = data_scan.ReadCompoundEntryHeaderView(dname);
              if (!dtag || *dtag == base::TagType::End) break;

              if (dname == "Motion" && *dtag == base::TagType::List) {
                auto m = base::ReadVec3DoubleList(data_scan);
                if (!m) return std::unexpected(m.error());
                ent.motion = *m;
              } else if (dname == "Rotation" && *dtag == base::TagType::List) {
                auto r = base::ReadVec2FloatList(data_scan);
                if (!r) return std::unexpected(r.error());
                ent.rotation = *r;
              } else {
                auto s2 = base::SkipPayload(data_scan, *dtag);
                if (!s2) return std::unexpected(s2.error());
              }
            }
          }
        } else {
          return base::SkipPayload(reader, t);
        }
        return {};
      });
  if (!result) return std::unexpected(result.error());

  // Build ent.data
  if (!is_v3) {
    // v2: raw compound body (Id + Pos + extra fields + End).
    ent.data = reader.SpanFrom(start);
  } else {
    // v3: re-serialize as Id + Pos + data_payload (no End tag at tail;
    // the data_payload's own End tag terminates the compound body).
    base::NbtWriter writer;
    writer.BeginListElementCompound();
    writer.WriteStringField("Id", id);
    writer.BeginListField("Pos", base::TagType::Double, 3);
    writer.WriteListElementDouble(ent.pos[0]);
    writer.WriteListElementDouble(ent.pos[1]);
    writer.WriteListElementDouble(ent.pos[2]);
    writer.EndListField();
    if (!data_payload.empty()) {
      writer.WriteListElementRawPayload(data_payload);
    }
    writer.EndListElementCompound();
    std::vector<std::byte> full_payload = std::move(writer).Finalize();
    if (!full_payload.empty() && full_payload.back() == std::byte{0})
      full_payload.pop_back();
    auto* mem = arena.Allocate(full_payload.size());
    std::memcpy(mem, full_payload.data(), full_payload.size());
    ent.data = std::span<const std::byte>(static_cast<const std::byte*>(mem),
                                          full_payload.size());
  }
  return ent;
}

[[nodiscard]] ParseResult<void> ParseEntities(base::ByteReader& reader,
                                              std::vector<Entity>& out,
                                              bool is_v3,
                                              memory::Arena& arena) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;
  if (elem_type == base::TagType::End || count == 0) return {};
  if (elem_type != base::TagType::Compound)
    return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
  if (count > reader.limits().max_entities)
    return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));

  out.clear();
  out.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    auto ent = ParseEntityCompound(reader, is_v3, arena);
    if (!ent) return std::unexpected(ent.error());
    out.push_back(std::move(*ent));
  }
  return {};
}

}  // namespace fschema::schem::internal