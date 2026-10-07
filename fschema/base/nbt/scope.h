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

#ifndef FSCHEMA_BASE_NBT_SCOPE_H_
#define FSCHEMA_BASE_NBT_SCOPE_H_

#include <string_view>

#include "fschema/base/error.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/base/port.h"

namespace fschema::base {

class DepthGuard {
 public:
  explicit DepthGuard(ByteReader& reader) noexcept : reader_(reader) {
    reader_.push_depth();
  }
  ~DepthGuard() noexcept { reader_.pop_depth(); }

  DepthGuard(const DepthGuard&) = delete;
  DepthGuard& operator=(const DepthGuard&) = delete;
  DepthGuard(DepthGuard&&) = delete;
  DepthGuard& operator=(DepthGuard&&) = delete;

 private:
  ByteReader& reader_;
};

template <typename Fn>
FSCHEMA_ALWAYS_INLINE [[nodiscard]] ParseResult<void> ForEachCompoundField(
    ByteReader& reader,
    Fn&& fn) {
  DepthGuard guard(reader);
  if (reader.depth() > reader.limits().max_nbt_depth) [[unlikely]] {
    return std::unexpected(reader.Error(ParseError::Code::DepthLimitExceeded));
  }
  for (;;) {
    std::string_view name;
    auto tag = reader.ReadCompoundEntryHeaderView(name);
    if (!tag) return std::unexpected(tag.error());
    if (*tag == TagType::End) [[unlikely]] break;
    auto result = fn(name, *tag);
    if (!result) return std::unexpected(result.error());
  }
  return {};
}

}  // namespace fschema::base

#endif  // FSCHEMA_BASE_NBT_SCOPE_H_