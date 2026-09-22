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

#ifndef FSCHEMA_PARSER_SCHEM_PARSE_H_
#define FSCHEMA_PARSER_SCHEM_PARSE_H_

#include <expected>
#include <filesystem>
#include <span>
#include <vector>

#include "parser/error.h"
#include "parser/schem/types.h"

namespace fschema::parser::schem {

  // Decompresses and parses a .schem file.
  // Supports both v2 and v3 of the Sponge Schematic specification.
  // Decompressed bytes are owned by Schematic::owner
  // All string_views and byte spans point into this buffer.
  // When Schematic is destroyed, all views and spans are invalid.
  [[nodiscard]] std::expected<Schematic, ParseError>
    ParseSchematic(const std::filesystem::path& path);

  // Parses from pre-decompressed NBT bytes, TAKING OWNERSHIP of the buffer.
  // Postcondition: `data` is left in a valid-but-unspecified (moved-from) state.
  [[nodiscard]] std::expected<Schematic, ParseError>
    ParseSchematicFromBytes(std::vector<std::byte> data);

  // Parses from a non-owning span of pre-decompressed NBT bytes.
  [[nodiscard]] std::expected<Schematic, ParseError>
    ParseSchematicFromBytes(std::span<const std::byte> data);

}  // namespace fschema::parser::schem

#endif  // FSCHEMA_PARSER_SCHEM_PARSE_H_