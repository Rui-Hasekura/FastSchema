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

#ifndef FSCHEMA_GEOMETRY_INTERNAL_PNG_ENCODE_H_
#define FSCHEMA_GEOMETRY_INTERNAL_PNG_ENCODE_H_

#include <cstdint>
#include <span>
#include <vector>

namespace fschema::geometry::internal {

// Encodes an RGBA8 buffer as a PNG file (stored-deflate, no compression).
// `rgba` must have exactly width * height * 4 bytes.
[[nodiscard]] std::vector<std::uint8_t> EncodePng(
    std::uint32_t width,
    std::uint32_t height,
    std::span<const std::uint8_t> rgba);

}  // namespace fschema::geometry::internal

#endif  // FSCHEMA_GEOMETRY_INTERNAL_PNG_ENCODE_H_
