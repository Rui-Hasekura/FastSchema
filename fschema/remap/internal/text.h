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

#ifndef FSCHEMA_REMAP_TEXT_H_
#define FSCHEMA_REMAP_TEXT_H_

#include <deque>
#include <optional>
#include <string>
#include <string_view>

#include "fschema/base/nbt/tree.h"

namespace fschema::remap::internal {

class RemapContext;

// Wraps a plain-text string into the target-era JSON text-component form
// (`{"text": "<escaped>"}`).
[[nodiscard]] std::string WrapPlainTextAsJson(std::string_view text);

// Converts a structured (NBT) text component into its JSON string form.
// Returns nullopt when the input is not convertible.
[[nodiscard]] std::optional<std::string> StructuredTextToJson(
    base::TagType type,
    const base::NbtPayload& payload);

// Downgrades the text component stored under `field` in `parent` to JSON.
// No-op when the field is absent.
void DowngradeTextFieldInPlace(RemapContext& ctx,
                               base::NbtCompound& parent,
                               std::string_view field,
                               std::uint16_t rule_id,
                               std::string_view path,
                               std::deque<std::string>& scratch);

// Walks a text component tree and pre-converts any `show_item` payloads
// inside `hover_event` to the target-era item tag form.
void PreprocessTextHoverItems(RemapContext& ctx,
                              base::NbtCompound& c,
                              std::deque<std::string>& scratch,
                              std::string_view path);

}  // namespace fschema::remap::internal

#endif  // FSCHEMA_REMAP_TEXT_H_