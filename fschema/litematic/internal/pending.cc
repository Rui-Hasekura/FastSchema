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

#include "fschema/litematic/internal/pending.h"

#include <cstdint>
#include <expected>
#include <utility>

#include "fschema/base/nbt_scope.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"

namespace fschema::litematic::internal {

[[nodiscard]] ParseResult<void> ParsePendingTicks(
    base::ByteReader& reader,
    std::vector<PendingTick>& out) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;

  if (elem_type == base::TagType::End || count == 0) return {};
  if (elem_type != base::TagType::Compound)
    return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
  if (count > reader.limits().max_pending_ticks)
    return std::unexpected(reader.Error(ParseError::Code::OversizedPayload));

  out.clear();
  out.reserve(count);

  base::DepthGuard list_guard(reader);
  if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::DepthLimitExceeded));
  }

  for (std::size_t i = 0; i < count; ++i) {
    PendingTick tick;
    bool have_block = false;

    auto result = ForEachCompoundField(
        reader,
        [&](std::string_view field, base::TagType t) -> ParseResult<void> {
          if (field == "Block" && t == base::TagType::String) {
            auto v = reader.ReadStringView();
            if (!v) return std::unexpected(v.error());
            tick.block = *v;
            have_block = true;
          } else if (field == "SubTick" && t == base::TagType::Long) {
            auto v = reader.Read<std::int64_t>();
            if (!v) return std::unexpected(v.error());
            tick.sub_tick = *v;
          } else if (field == "Priority" && t == base::TagType::Int) {
            auto v = reader.Read<std::int32_t>();
            if (!v) return std::unexpected(v.error());
            tick.priority = *v;
          } else if (field == "Time" && t == base::TagType::Int) {
            auto v = reader.Read<std::int32_t>();
            if (!v) return std::unexpected(v.error());
            tick.time = *v;
          } else if (field == "x" && t == base::TagType::Int) {
            auto v = reader.Read<std::int32_t>();
            if (!v) return std::unexpected(v.error());
            tick.pos[0] = *v;
          } else if (field == "y" && t == base::TagType::Int) {
            auto v = reader.Read<std::int32_t>();
            if (!v) return std::unexpected(v.error());
            tick.pos[1] = *v;
          } else if (field == "z" && t == base::TagType::Int) {
            auto v = reader.Read<std::int32_t>();
            if (!v) return std::unexpected(v.error());
            tick.pos[2] = *v;
          } else {
            return base::SkipPayload(reader, t);
          }
          return {};
        });
    if (!result) return std::unexpected(result.error());

    if (!have_block) [[unlikely]] {
      return std::unexpected(reader.Error(ParseError::Code::MissingField));
    }
    out.push_back(std::move(tick));
  }
  return {};
}

}  // namespace fschema::litematic::internal