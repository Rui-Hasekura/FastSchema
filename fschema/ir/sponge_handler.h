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

#ifndef FSCHEMA_IR_SPONGE_HANDLER_H_
#define FSCHEMA_IR_SPONGE_HANDLER_H_

#include "fschema/ir/format_handler.h"
#include "fschema/sponge/types.h"

namespace fschema::ir::format {

class SpongeHandler final : public FormatHandler {
 public:
  [[nodiscard]] SourceFormat format() const noexcept override {
    return SourceFormat::kSponge;
  }

  [[nodiscard]] ParseResult<Schematic> Decode(
      std::unique_ptr<std::vector<std::byte>> bytes,
      const base::DecodeLimits& limits) const override;

  [[nodiscard]] ParseResult<std::vector<std::byte>> Encode(
      const Schematic& schematic,
      const EncodeOptions& options) const override;

  [[nodiscard]] ParseResult<Schematic> DecodeFromParsed(
      sponge::Schematic&& src) const;
};

}  // namespace fschema::ir::format

#endif  // FSCHEMA_IR_SPONGE_HANDLER_H_