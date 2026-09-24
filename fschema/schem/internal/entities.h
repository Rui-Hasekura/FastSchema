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

#ifndef FSCHEMA_SCHEM_INTERNAL_ENTITIES_H_
#define FSCHEMA_SCHEM_INTERNAL_ENTITIES_H_

#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/schem/types.h"

namespace fschema::schem::internal {

  // Parses an Entities List<Compound>.
  // v2: { Pos: List<Double>[3], Id: String, ...extra fields directly }
  // v3: { Pos: List<Double>[3], Id: String, Data: Compound {extra} }
  [[nodiscard]] ParseResult<void> ParseEntities(
    base::ByteReader& reader,
    std::vector<Entity>& out,
    bool is_v3);

}  // namespace fschema::schem::internal

#endif  // FSCHEMA_SCHEM_INTERNAL_ENTITIES_H_