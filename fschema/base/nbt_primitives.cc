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

#include "fschema/base/nbt_primitives.h"

#include <array>
#include <cstddef>
#include <expected>
#include <utility>

#include "fschema/base/error.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_skip.h"
#include "fschema/base/nbt_tag.h"

namespace fschema::base {

namespace {

template <typename T, std::size_t N>
[[nodiscard]] ParseResult<std::array<T, N>> ReadVecList(ByteReader& reader,
                                                        TagType expected_type) {
  auto header = reader.ReadListHeader();
  if (!header) return std::unexpected(header.error());
  auto [elem_type, count] = *header;

  std::array<T, N> result{};
  if (elem_type == TagType::End || count == 0) return result;
  if (elem_type != expected_type) {
    return std::unexpected(reader.Error(ParseError::Code::InvalidTagId));
  }
  if (count != N) {
    // Length mismatch: consume the values but discard.
    for (std::size_t i = 0; i < count; ++i) {
      auto r = SkipPayload(reader, expected_type);
      if (!r) return std::unexpected(r.error());
    }
    return result;
  }
  for (std::size_t i = 0; i < N; ++i) {
    auto v = reader.Read<T>();
    if (!v) return std::unexpected(v.error());
    result[i] = *v;
  }
  return result;
}

}  // namespace

ParseResult<std::array<double, 3>> ReadVec3DoubleList(ByteReader& reader) {
  return ReadVecList<double, 3>(reader, TagType::Double);
}

ParseResult<std::array<float, 2>> ReadVec2FloatList(ByteReader& reader) {
  return ReadVecList<float, 2>(reader, TagType::Float);
}

}  // namespace fschema::base