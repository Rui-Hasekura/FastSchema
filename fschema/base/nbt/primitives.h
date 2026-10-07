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

#ifndef FSCHEMA_BASE_NBT_PRIMITIVES_H_
#define FSCHEMA_BASE_NBT_PRIMITIVES_H_

#include <array>

#include "fschema/base/error.h"
#include "fschema/base/nbt/reader.h"

namespace fschema::base {

// Reads a List<Double>[3]. Returns {0,0,0} for empty/End list, returns
// InvalidTagId if element type is not Double, returns {0,0,0} and skips
// all elements if length != 3.
[[nodiscard]] ParseResult<std::array<double, 3>> ReadVec3DoubleList(
    ByteReader& reader);

// Reads a List<Float>[2]. Same semantics as ReadVec3DoubleList but for 2
// floats.
[[nodiscard]] ParseResult<std::array<float, 2>> ReadVec2FloatList(
    ByteReader& reader);

}  // namespace fschema::base

#endif  // FSCHEMA_BASE_NBT_PRIMITIVES_H_