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

#ifndef FSCHEMA_PARSER_SCHEM_DETAIL_ROOT_H_
#define FSCHEMA_PARSER_SCHEM_DETAIL_ROOT_H_

#include <expected>

#include "parser/error.h"
#include "parser/nbt/reader.h"
#include "parser/schem/types.h"

namespace fschema::parser::schem::detail {

  // Root parser entry point.
  //
  // v2: root NBT tag is TAG_Compound("Schematic"),
  //     fields are directly in the root compound.
  // v3: root NBT tag is TAG_Compound(""),
  //     contains a TAG_Compound("Schematic") child.
  //
  // Detection:
  //   1. Read root tag byte (must be 0x0A).
  //   2. Read root name.
  //   3. If root name == "Schematic" -> v2 path.
  //   4. Otherwise -> v3 path (scan for "Schematic" child).
  [[nodiscard]] ParseResult<void> ParseRoot(
    nbt::ByteReader& reader, Schematic& out);

}  // namespace fschema::parser::schem::detail

#endif  // FSCHEMA_PARSER_SCHEM_DETAIL_ROOT_H_