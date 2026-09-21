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

#include "parser/litematic/parse.h"

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "parser/arena.h"
#include "parser/error.h"
#include "parser/limits.h"
#include "parser/litematic/detail/root.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"

namespace fschema::parser::litematic {

  [[nodiscard]] ParseResult<Litematic> ParseLitematic(
    std::unique_ptr<std::vector<std::byte>> decompressed,
    const DecodeLimits& limits) {

    Litematic out;
    out.arena = std::make_unique<Arena>();

    if (!decompressed || decompressed->empty()) {
      return std::unexpected(ParseError::At(
        ParseError::Code::Truncated, std::string{}, 0));
    }
    out.owner = std::move(decompressed);

    std::span<const std::byte> buffer{ out.owner->data(), out.owner->size() };
    nbt::ByteReader reader(buffer, limits);

    auto result = detail::ParseRoot(reader, out);
    if (!result) return std::unexpected(result.error());
    return out;
  }

} // namespace fschema::parser::litematic