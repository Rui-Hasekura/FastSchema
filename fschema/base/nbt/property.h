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

#ifndef FSCHEMA_BASE_NBT_PROPERTY_H_
#define FSCHEMA_BASE_NBT_PROPERTY_H_

#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include "fschema/base/limits.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/tag.h"

namespace fschema::base {

template <typename Fn>
inline bool ForEachNbtStringProperty(std::span<const std::byte> nbt, Fn&& fn) {
  if (nbt.empty()) return true;
  ByteReader reader(nbt, DecodeLimits{});
  for (;;) {
    std::string_view key;
    auto tag = reader.ReadCompoundEntryHeaderView(key);
    if (!tag || *tag == TagType::End) break;
    if (*tag != TagType::String) {
      if (!SkipPayload(reader, *tag)) return false;
      continue;
    }
    auto value = reader.ReadStringView();
    if (!value) return false;
    if (!fn(key, *value)) return false;
  }
  return true;
}

[[nodiscard]] inline std::optional<std::string_view> FindNbtStringProp(
    std::span<const std::byte> nbt,
    std::string_view key) {
  std::optional<std::string_view> result;
  ForEachNbtStringProperty(nbt, [&](std::string_view k, std::string_view v) {
    if (k == key) {
      result = v;
      return false;
    }
    return true;
  });
  return result;
}

}  // namespace fschema::base

#endif  // FSCHEMA_BASE_NBT_PROPERTY_H_