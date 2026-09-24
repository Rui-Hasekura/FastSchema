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

#ifndef FSCHEMA_BASE_NBT_PARSE_H_
#define FSCHEMA_BASE_NBT_PARSE_H_

#include <expected>

#include "fschema/base/error.h"
#include "fschema/base/nbt_tree.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_tag.h"

namespace fschema::base {

  [[nodiscard]] ParseResult<NbtPayload> ParsePayload(ByteReader& reader,
                                                     TagType tag_type);
  [[nodiscard]] ParseResult<NbtCompound> ParseCompound(ByteReader& reader);
  [[nodiscard]] ParseResult<NbtList> ParseList(ByteReader& reader);
  [[nodiscard]] ParseResult<NbtTag> ParseNbt(ByteReader& reader);

}  // namespace fschema::base

#endif  // FSCHEMA_BASE_NBT_PARSE_H_