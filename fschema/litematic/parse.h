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

#ifndef FSCHEMA_LITEMATIC_PARSE_H_
#define FSCHEMA_LITEMATIC_PARSE_H_

#include <cstddef>
#include <memory>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/litematic/types.h"

namespace fschema::litematic {

  // Entry: ParseLitematic after DecompressGzipFile
  //
  // bytes' ownership is transferred to the returned Litematic::owner.
  // All internal spans (properties / raw_nbt / preview_data) point to it.
  // This span is valid until the Litematic object is destructed.
  //
  // Precondition: bytes must be a valid gzip decompressed result
  // (guaranteed by the upper layer DecompressGzipFile).
  [[nodiscard]] ParseResult<Litematic> ParseLitematic(
    std::unique_ptr<std::vector<std::byte>> decompressed,
    const fschema::base::DecodeLimits& limits = {});

} // namespace fschema::litematic

#endif // FSCHEMA_LITEMATIC_PARSE_H_