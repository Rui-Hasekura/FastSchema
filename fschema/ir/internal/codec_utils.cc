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

#include "fschema/ir/internal/codec_utils.h"

#include <cstddef>
#include <span>
#include <string_view>

#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"
#include "fschema/base/nbt_writer.h"
#include "fschema/base/port.h"

namespace fschema::ir::internal {

namespace {

inline bool IsSkipName(std::string_view name,
                       std::span<const std::string_view> skip_names) noexcept {
  for (auto s : skip_names) {
    if (name == s) return true;
  }
  return false;
}

}  // namespace

FSCHEMA_HOT ParseResult<void> FilterAndWriteFields(
    std::span<const std::byte> raw_nbt,
    std::span<const std::string_view> skip_names,
    fschema::base::NbtWriter& writer) {
  if (raw_nbt.empty()) return {};
  fschema::base::ByteReader reader(raw_nbt, fschema::base::DecodeLimits{});
  for (;;) {
    std::string_view name;
    const std::size_t field_start = reader.pos();
    auto tag = reader.ReadCompoundEntryHeaderView(name);
    if (!tag) return std::unexpected(tag.error());
    if (*tag == fschema::base::TagType::End) break;
    auto skip = fschema::base::SkipPayload(reader, *tag);
    if (!skip) return std::unexpected(skip.error());
    if (!IsSkipName(name, skip_names)) {
      writer.WriteListElementRawPayload(reader.SpanFrom(field_start));
    }
  }
  return {};
}

FSCHEMA_HOT ParseResult<void> FilterAndWriteTileEntityFields(
    std::span<const std::byte> raw_nbt,
    std::span<const std::string_view> skip_names,
    fschema::base::NbtWriter& writer) {
  if (raw_nbt.empty()) return {};
  fschema::base::ByteReader reader(raw_nbt, fschema::base::DecodeLimits{});
  for (;;) {
    std::string_view name;
    const std::size_t field_start = reader.pos();
    auto tag = reader.ReadCompoundEntryHeaderView(name);
    if (!tag) return std::unexpected(tag.error());
    if (*tag == fschema::base::TagType::End) break;

    if (name == "components" && *tag == fschema::base::TagType::Compound) {
      const std::size_t comp_start = reader.pos();
      auto skip = fschema::base::SkipPayload(reader, *tag);
      if (!skip) return std::unexpected(skip.error());
      auto comp_body = reader.SpanFrom(comp_start);
      auto res = FilterAndWriteFields(comp_body, skip_names, writer);
      if (!res) return std::unexpected(res.error());
    } else {
      auto skip = fschema::base::SkipPayload(reader, *tag);
      if (!skip) return std::unexpected(skip.error());
      if (!IsSkipName(name, skip_names)) {
        writer.WriteListElementRawPayload(reader.SpanFrom(field_start));
      }
    }
  }
  return {};
}

}  // namespace fschema::ir::internal