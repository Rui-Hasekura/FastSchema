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

#include "fschema/remap/remap.h"

#include <cstdint>
#include <string>
#include <utility>

#include "fschema/remap/internal/context.h"
#include "fschema/remap/internal/rule.h"
#include "fschema/remap/internal/palette.h"
#include "fschema/remap/internal/entity.h"
#include "fschema/remap/internal/block_entity.h"

namespace fschema::remap {

namespace {

// Number of rule rows whose anchor falls in the active (target, source]
// window. Counts the text family and entity-struct family as one each
// when their window gates are active. Zero means the pass leaves the data
// untouched.
[[nodiscard]] std::uint32_t CountApplicableRules(std::int32_t target_dv,
                                                 std::int32_t source_dv,
                                                 RemapDirection dir) noexcept {
  if (dir != RemapDirection::kDowngrade) return 0;

  std::uint32_t n = 0;
  for (const auto& r : kComponentRules) {
    if (RuleActive(target_dv, source_dv, r.anchor_dv)) ++n;
  }
  n += CountActiveIdRenames(target_dv, source_dv);
  n += CountActiveAttributeRenames(target_dv, source_dv);
  if (TextFamilyActive(target_dv, source_dv, dir)) ++n;
  if (EntityStructFamilyActive(target_dv, source_dv, dir)) ++n;
  return n;
}

[[nodiscard]] std::string IndexPath(std::string_view base,
                                    std::size_t i,
                                    std::string_view field) {
  std::string out;
  out.reserve(base.size() + field.size() + 12);
  out += base;
  if (!field.empty()) {
    out += '.';
    out += field;
  }
  out += '[';
  out += std::to_string(i);
  out += ']';
  return out;
}

}  // namespace

ParseResult<RemapStats> RemapToDataVersion(ir::Schematic& schematic,
                                           std::int32_t target_data_version,
                                           const RemapOptions& options) {
  RemapStats stats;

  // Reject targets the pass cannot handle.
  if (target_data_version <= 0) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kUnsupportedVersion,
                       "RemapToDataVersion: invalid target DataVersion",
                       0));
  }
  if (ContainerForDataVersion(target_data_version) == 5) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kUnsupportedVersion,
        "RemapToDataVersion: targets below DataVersion 2860 (litematica v5)"
        " are rejected by policy",
        0));
  }

  if (schematic.data_version == target_data_version) {
    internal::RemapContext ctx(schematic, stats, options, target_data_version);
    ctx.Report(Diag::kNoOpAtTarget, 0, "");
    return stats;
  }

  internal::RemapContext ctx(schematic, stats, options, target_data_version);

  const std::uint32_t active =
      CountApplicableRules(ctx.tgt_dv(), ctx.src_dv(), ctx.direction());
  stats.active_rules = active;

  if (active == 0) {
    // No rules cover the (target, source] window; data and version fields
    // stay untouched.
    ctx.Report(Diag::kNoRulesCoverage, 0, "");
    return stats;
  }

  // Family order: palette, block entities, entities. Each family runs its
  // own pre-scan internally.
  for (std::size_t i = 0; i < schematic.regions.size(); ++i) {
    ir::Region& region = schematic.regions[i];
    const std::string region_path = IndexPath("Regions", i, "");

    if (auto r = internal::RemapRegionPalette(ctx, region, region_path);
        !r.has_value()) {
      return std::unexpected(r.error());
    }

    for (std::size_t j = 0; j < region.block_entities.size(); ++j) {
      const std::string path = IndexPath(region_path, j, "TileEntities");
      if (auto r =
              internal::RemapBlockEntity(ctx, region.block_entities[j], path);
          !r.has_value()) {
        return std::unexpected(r.error());
      }
    }

    for (std::size_t k = 0; k < region.entities.size(); ++k) {
      const std::string path = IndexPath(region_path, k, "Entities");
      if (auto r = internal::RemapEntity(ctx, region.entities[k], path);
          !r.has_value()) {
        return std::unexpected(r.error());
      }
    }
  }

  // Record the new DataVersion and its implied container version.
  schematic.data_version = target_data_version;
  schematic.source_version = ContainerForDataVersion(target_data_version);

  return stats;
}

ParseResult<void> MaybeRemapForTarget(ir::Schematic& schematic,
                                      ir::SourceFormat target_format,
                                      std::int32_t target_container_version,
                                      const RemapOptions& remap_options,
                                      RemapStats* stats_out) {
  std::int32_t target_dv = remap_options.target_data_version;
  if (target_dv == 0) {
    target_dv =
        ResolveDataVersionForContainer(target_format, target_container_version);
  }
  if (target_dv == -1) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kUnsupportedVersion,
                       "litematica v5 target rejected by remap policy",
                       0));
  }
  if (target_dv == 0) return {};  // no remap requested (round-trip)

  if (schematic.data_version == target_dv) {
    if (stats_out != nullptr) {
      RemapStats& s = *stats_out;
      s.source_data_version = schematic.data_version;
      s.target_data_version = target_dv;
      s.watchlist.Set(static_cast<std::uint32_t>(Diag::kNoOpAtTarget));
    }
    return {};
  }

  auto res = RemapToDataVersion(schematic, target_dv, remap_options);
  if (!res) return std::unexpected(res.error());
  if (stats_out != nullptr) *stats_out = std::move(*res);
  return {};
}

}  // namespace fschema::remap