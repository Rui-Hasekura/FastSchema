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

#include "fschema/schem/parse.h"

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "fschema/base/decompression.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/memory/arena.h"
#include "fschema/schem/internal/root.h"
#include "fschema/schem/types.h"

namespace fschema::schem {

  namespace {

    // Core parser entry: takes ownership of `owner_bytes` and parses NBT
    // directly from it.
    // All subsequent string_views / spans in the parsed Schematic point into
    // this buffer; lifetime is tied to Schematic::owner.
    [[nodiscard]] std::expected<Schematic, ParseError>
      ParseSchematicFromOwnedBuffer(std::vector<std::byte> owner_bytes) {
      Schematic schematic;
      schematic.arena = std::make_unique<memory::Arena>();

      // Move-construct owner. This is the ONLY assignment to owner;
      schematic.owner = std::make_unique<std::vector<std::byte>>(
        std::move(owner_bytes));

      base::DecodeLimits limits;
      base::ByteReader reader(
        std::span<const std::byte>(
          schematic.owner->data(),
          schematic.owner->size()),
        limits);

      auto result = internal::ParseRoot(reader, schematic);
      if (!result) {
        return std::unexpected(result.error());
      }

      return schematic;
    }

  }  // namespace

  [[nodiscard]] std::expected<Schematic, ParseError>
    ParseSchematic(const std::filesystem::path& path) {
    auto decompressed = base::DecompressGzipFile(path);
    if (!decompressed) {
      return std::unexpected(ParseError{
          ParseError::Code::Truncated, path.string(), 0 });
    }

    // Move the decompressed buffer straight into the parser core.
    return ParseSchematicFromOwnedBuffer(std::move(*decompressed));
  }

  [[nodiscard]] std::expected<Schematic, ParseError>
    ParseSchematicFromBytes(std::vector<std::byte> data) {
    return ParseSchematicFromOwnedBuffer(std::move(data));
  }

  // Copy overload for non-owning callers.
  [[nodiscard]] std::expected<Schematic, ParseError>
    ParseSchematicFromBytes(std::span<const std::byte> data) {
    std::vector<std::byte> copy(data.begin(), data.end());
    return ParseSchematicFromOwnedBuffer(std::move(copy));
  }

}  // namespace fschema::schem