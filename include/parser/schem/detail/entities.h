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

#ifndef FSCHEMA_PARSER_SCHEM_DETAIL_ENTITIES_H_
#define FSCHEMA_PARSER_SCHEM_DETAIL_ENTITIES_H_

#include <expected>
#include <vector>

#include "parser/error.h"
#include "parser/nbt/reader.h"
#include "parser/schem/types.h"

namespace fschema::parser::schem::detail {

  // Parses an Entities List<Compound>.
  // v2: { Pos: List<Double>[3], Id: String, ...extra fields directly }
  // v3: { Pos: List<Double>[3], Id: String, Data: Compound {extra} }
  [[nodiscard]] ParseResult<void> ParseEntities(
    nbt::ByteReader& reader,
    std::vector<Entity>& out,
    bool is_v3);

}  // namespace fschema::parser::schem::detail

#endif  // FSCHEMA_PARSER_SCHEM_DETAIL_ENTITIES_H_