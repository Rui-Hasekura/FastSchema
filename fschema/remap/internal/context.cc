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

#include "fschema/remap/internal/context.h"

#include <cstring>
#include <utility>

#include "fschema/memory/arena.h"

namespace fschema::remap::internal {

RemapContext::RemapContext(ir::Schema& schema,
                           RemapStats& stats,
                           const RemapOptions& options,
                           std::int32_t target_dv)
    : schema_(schema),
      stats_(stats),
      options_(options),
      src_dv_(schema.data_version),
      tgt_dv_(target_dv),
      dir_(schema.data_version > target_dv ? RemapDirection::kDowngrade
                                           : RemapDirection::kUpgrade) {
  stats_.direction = dir_;
  stats_.source_data_version = src_dv_;
  stats_.target_data_version = tgt_dv_;
}

std::span<const std::byte> RemapContext::PersistBytes(
    const std::vector<std::byte>& bytes) noexcept {
  if (bytes.empty()) return {};
  // The Schema's arena outlives every IR span, so spans returned here stay
  // valid for the Schema's remaining lifetime. Alignment 8 is sufficient.
  // Returns an empty span on arena failure; the caller keeps the original
  // bytes in that case.
  void* p = schema_.arena->Allocate(bytes.size(), 8);
  if (p == nullptr) return {};
  std::memcpy(p, bytes.data(), bytes.size());
  return std::span<const std::byte>(static_cast<const std::byte*>(p),
                                    bytes.size());
}

void RemapContext::Report(Diag code,
                          std::uint16_t rule_id,
                          std::string_view path) noexcept {
  const std::uint32_t c = static_cast<std::uint32_t>(code);
  stats_.watchlist.Set(c);
  if (stats_.diagnostics.size() < options_.max_diagnostics) {
    Diagnostic d;
    d.code = c;
    d.rule_id = rule_id;
    d.path.assign(path);
    stats_.diagnostics.push_back(std::move(d));
  } else {
    ++stats_.dropped_diagnostics;
  }
}

}  // namespace fschema::remap::internal