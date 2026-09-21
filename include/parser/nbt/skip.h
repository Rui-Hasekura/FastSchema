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

#ifndef FSCHEMA_PARSER_NBT_SKIP_H_
#define FSCHEMA_PARSER_NBT_SKIP_H_

#include <expected>

#include "parser/error.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/tag.h"

namespace fschema::parser::nbt {

  [[nodiscard]] ParseResult<void> SkipPayload(ByteReader& reader,
                                              TagType tag_type);

  [[nodiscard]] ParseResult<void> SkipCompound(ByteReader& reader);

  [[nodiscard]] ParseResult<void> SkipList(ByteReader& reader);

  [[nodiscard]] ParseResult<void> SkipScalar(ByteReader& reader,
                                             TagType tag_type);

  [[nodiscard]] ParseResult<void> SkipString(ByteReader& reader);

  [[nodiscard]] ParseResult<void> SkipArray(ByteReader& reader,
                                            TagType tag_type);

}  // namespace fschema::parser::nbt

#endif  // FSCHEMA_PARSER_NBT_SKIP_H_