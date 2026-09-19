/*
 * Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef FSCHEMA_PARSER_LITEMATIC_DETAIL_ROOT_H_
#define FSCHEMA_PARSER_LITEMATIC_DETAIL_ROOT_H_

#include <expected>

#include "parser/error.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"

namespace fschema::parser::litematic::detail {

  // Litematica root Compound fields (Java write order is fixed):
  //   Version: Int          <- Necessary
  //   SubVersion: Int       <- v6+ Optional, Skip
  //   DataVersion: Int      <- v5+ Necessary
  //   Metadata: Compound    <- Necessary
  //   Regions: Compound     <- Necessary
  //
  // Unknown field -> SkipPayload
  [[nodiscard]] ParseResult<void> ParseRoot(
    nbt::ByteReader& reader, Litematic& out);

} // namespace fschema::parser::litematic::detail

#endif // FSCHEMA_PARSER_LITEMATIC_DETAIL_ROOT_H_