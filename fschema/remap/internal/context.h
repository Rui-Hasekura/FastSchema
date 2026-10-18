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

#ifndef FSCHEMA_REMAP_CONTEXT_H_
#define FSCHEMA_REMAP_CONTEXT_H_

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include "fschema/remap/remap.h"
#include "fschema/remap/internal/rule.h"

namespace fschema::remap::internal {

// Per-pass state shared by every rule family.
//
// Roots (block entity / entity raw NBT spans) are first pre-scanned with a
// cheap byte-level pattern search. Unhit roots pass through unchanged
// (their spans keep pointing into the source buffer). Hit roots are
// materialized into an NbtCompound tree, transformed, serialized, and the
// result is persisted into the Schema's arena.
class RemapContext {
 public:
  RemapContext(ir::Schema& schema,
               RemapStats& stats,
               const RemapOptions& options,
               std::int32_t target_dv);

  [[nodiscard]] bool ItemFamilyActive() const noexcept {
    return remap::ItemFamilyActive(tgt_dv_, src_dv_, dir_);
  }
  [[nodiscard]] bool TextFamilyActive() const noexcept {
    return remap::TextFamilyActive(tgt_dv_, src_dv_, dir_);
  }
  [[nodiscard]] bool IdRenameFamilyActive() const noexcept {
    return dir_ == RemapDirection::kDowngrade &&
           CountActiveIdRenames(tgt_dv_, src_dv_) != 0;
  }
  [[nodiscard]] bool AttributeFamilyActive() const noexcept {
    return dir_ == RemapDirection::kDowngrade &&
           CountActiveAttributeRenames(tgt_dv_, src_dv_) != 0;
  }
  [[nodiscard]] bool EntityStructFamilyActive() const noexcept {
    return remap::EntityStructFamilyActive(tgt_dv_, src_dv_, dir_);
  }

  // Copies `bytes` into the Schema's arena and returns a span over the copy.
  // The span remains valid for the lifetime of the Schema.
  [[nodiscard]] std::span<const std::byte> PersistBytes(
      const std::vector<std::byte>& bytes) noexcept;

  // Records a diagnostic up to options.max_diagnostics. Emits beyond the cap
  // are counted in `dropped_diagnostics`. Also sets the watchlist bit for
  // `code`.
  void Report(Diag code, std::uint16_t rule_id, std::string_view path) noexcept;

  [[nodiscard]] ir::Schema& schema() noexcept { return schema_; }
  [[nodiscard]] RemapStats& stats() noexcept { return stats_; }
  [[nodiscard]] const RemapOptions& options() const noexcept {
    return options_;
  }
  [[nodiscard]] std::int32_t src_dv() const noexcept { return src_dv_; }
  [[nodiscard]] std::int32_t tgt_dv() const noexcept { return tgt_dv_; }
  [[nodiscard]] RemapDirection direction() const noexcept { return dir_; }

 private:
  ir::Schema& schema_;
  RemapStats& stats_;
  RemapOptions options_;
  std::int32_t src_dv_;
  std::int32_t tgt_dv_;
  RemapDirection dir_;
};

}  // namespace fschema::remap::internal

#endif  // FSCHEMA_REMAP_CONTEXT_H_