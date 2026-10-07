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

#include "fschema/base/error.h"

#include <string>
#include <string_view>
#include <utility>

namespace fschema {

[[nodiscard]] ParseError ParseError::At(Code code,
                                        std::string path,
                                        std::size_t offset) {
  return ParseError{code, std::move(path), offset};
}

[[nodiscard]] std::string_view ToString(ParseError::Code code) noexcept {
  switch (code) {
    case ParseError::Code::Truncated:
      return "Truncated";
    case ParseError::Code::InvalidTagId:
      return "InvalidTagId";
    case ParseError::Code::NegativeLength:
      return "NegativeLength";
    case ParseError::Code::DepthLimitExceeded:
      return "DepthLimitExceeded";
    case ParseError::Code::OversizedPayload:
      return "OversizedPayload";
    case ParseError::Code::UnsupportedVersion:
      return "UnsupportedVersion";
    case ParseError::Code::MissingField:
      return "MissingField";
    case ParseError::Code::InvalidStructure:
      return "InvalidStructure";
    case ParseError::Code::BlockStatesTooSmall:
      return "BlockStatesTooSmall";
    case ParseError::Code::PaletteIndexOutOfRange:
      return "PaletteIndexOutOfRange";
    case ParseError::Code::VolumeOverflow:
      return "VolumeOverflow";
    case ParseError::Code::VarintOverflow:
      return "VarintOverflow";
    case ParseError::Code::BlockDataTooSmall:
      return "BlockDataTooSmall";
    case ParseError::Code::NegativeIndex:
      return "NegativeIndex";
  }
  return "Unknown";
}

}  // namespace fschema