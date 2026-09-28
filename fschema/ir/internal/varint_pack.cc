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

#include "fschema/ir/internal/varint_pack.h"

namespace fschema::ir::internal {

std::vector<std::byte> PackVarintSchem(std::span<const std::uint16_t> indices) {
  std::vector<std::byte> out;
  out.reserve(indices.size());  // Average case is 1 byte per block

  for (std::uint16_t val16 : indices) {
    std::uint32_t value = val16;
    while (value > 0x7F) {
      out.push_back(static_cast<std::byte>((value & 0x7F) | 0x80));
      value >>= 7;
    }
    out.push_back(static_cast<std::byte>(value));
  }
  return out;
}

}  // namespace fschema::ir::internal