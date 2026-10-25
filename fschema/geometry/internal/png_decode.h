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

#ifndef FSCHEMA_GEOMETRY_INTERNAL_PNG_DECODE_H_
#define FSCHEMA_GEOMETRY_INTERNAL_PNG_DECODE_H_

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry::internal {

// Decodes a PNG file into an RGBA8 DecodedTexture. On success `rgba`
// points into *owned_pixels, which receives the pixel storage and must
// outlive the span. On failure returns kTextureDecodeFailed; the error
// `path` carries the Wuffs status string (e.g. "bad PNG: bad
// signature"). Rejects images whose pixel buffer would exceed 256 MiB.
[[nodiscard]] ParseResult<DecodedTexture> DecodePng(
    std::span<const std::uint8_t> bytes,
    std::unique_ptr<std::vector<std::uint8_t>>& owned_pixels);

}  // namespace fschema::geometry::internal

#endif  // FSCHEMA_GEOMETRY_INTERNAL_PNG_DECODE_H_
