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

#include "fschema/litematic/parse.h"

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "fschema/memory/arena.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/litematic/internal/root.h"
#include "fschema/litematic/types.h"
#include "fschema/base/nbt_reader.h"

namespace fschema::litematic {

  [[nodiscard]] ParseResult<Litematic> ParseLitematic(
    std::unique_ptr<std::vector<std::byte>> decompressed,
    const base::DecodeLimits& limits) {

    Litematic out;
    out.arena = std::make_unique<memory::Arena>();

    if (!decompressed || decompressed->empty()) {
      return std::unexpected(ParseError::At(
        ParseError::Code::Truncated, std::string{}, 0));
    }
    out.owner = std::move(decompressed);

    std::span<const std::byte> buffer{ out.owner->data(), out.owner->size() };
    base::ByteReader reader(buffer, limits);

    auto result = internal::ParseRoot(reader, out);
    if (!result) return std::unexpected(result.error());
    return out;
  }

} // namespace fschema::litematic