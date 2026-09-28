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

#ifndef FSCHEMA_IR_SCHEM_HANDLER_H_
#define FSCHEMA_IR_SCHEM_HANDLER_H_

#include "fschema/ir/format_handler.h"
#include "fschema/schem/types.h"

namespace fschema::ir::format {

class SchemHandler final : public FormatHandler {
 public:
  [[nodiscard]] std::string_view id() const noexcept override {
    return "schem";
  }
  [[nodiscard]] SourceFormat format() const noexcept override {
    return SourceFormat::kSchem;
  }

  [[nodiscard]] ParseResult<Schema> Decode(
      std::unique_ptr<std::vector<std::byte>> bytes,
      const base::DecodeLimits& limits) const override;

  [[nodiscard]] ParseResult<std::vector<std::byte>> Encode(
      const Schema& ir,
      const EncodeOptions& options) const override;

  // High-efficiency path: build IR directly from already parsed Schematic
  [[nodiscard]] ParseResult<Schema> DecodeFromParsed(
      schem::Schematic&& src) const;
};

}  // namespace fschema::ir::format

#endif  // FSCHEMA_IR_SCHEM_HANDLER_H_