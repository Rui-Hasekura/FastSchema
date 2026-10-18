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

#include "fschema/remap/internal/item.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/remap/internal/block_entity.h"
#include "fschema/remap/internal/component_extra.h"
#include "fschema/remap/internal/context.h"
#include "fschema/remap/internal/entity.h"
#include "fschema/remap/internal/nbt_utils.h"
#include "fschema/remap/internal/rule.h"
#include "fschema/remap/internal/text.h"

namespace fschema::remap::internal {

namespace {

using base::NbtCompound;
using base::NbtList;
using base::NbtPayload;
using base::NbtTag;
using base::TagType;

// Maps a component name to its HideFlags bit (0 if none).
std::uint8_t HideFlagForComponent(std::string_view component) noexcept {
  if (component == "minecraft:enchantments") return kHideEnchantments;
  if (component == "minecraft:attribute_modifiers")
    return kHideAttributeModifiers;
  if (component == "minecraft:unbreakable") return kHideUnbreakable;
  if (component == "minecraft:can_break") return kHideCanDestroy;
  if (component == "minecraft:can_place_on") return kHideCanPlaceOn;
  if (component == "minecraft:bundle_contents") return kHideAdditional;
  if (component == "minecraft:dyed_color") return kHideDyed;
  if (component == "minecraft:trim") return kHideAdditional;
  if (component == "minecraft:stored_enchantments") return kHideEnchantments;
  return 0;
}

// ORs `bits` into the HideFlags byte on `tag`, creating it if absent.
void AccumulateHideFlags(NbtCompound& tag, std::uint8_t bits) noexcept {
  if (bits == 0) return;
  for (auto& child : tag.children) {
    if (child.name == "HideFlags" && child.type == TagType::kByte) {
      if (const auto* v = std::get_if<std::int8_t>(&child.payload);
          v != nullptr) {
        child.payload =
            static_cast<std::int8_t>(*v | static_cast<std::int8_t>(bits));
        return;
      }
    }
  }
  tag.children.push_back(
      MakeByteTag("HideFlags", static_cast<std::int8_t>(bits)));
}

// Axolotl variant name -> numeric id (target-era Variant tag).
std::optional<std::int32_t> AxolotlVariantMap(std::string_view s) noexcept {
  if (s == "lucy") return 0;
  if (s == "wild") return 1;
  if (s == "gold") return 2;
  if (s == "cyan") return 3;
  if (s == "blue") return 4;
  return std::nullopt;
}

// Tropical fish pattern name -> packed pattern id.
std::optional<std::int32_t> TropicalFishPatternMap(
    std::string_view s) noexcept {
  if (s == "kob") return 0;
  if (s == "sunstreak") return 1;
  if (s == "snooper") return 2;
  if (s == "dasher") return 3;
  if (s == "brinely") return 4;
  if (s == "spotty") return 5;
  if (s == "flopper") return 1 << 8;
  if (s == "stripey") return (1 << 8) | 1;
  if (s == "glitter") return (1 << 8) | 2;
  if (s == "blockfish") return (1 << 8) | 3;
  if (s == "betty") return (1 << 8) | 4;
  if (s == "clayfish") return (1 << 8) | 5;
  return std::nullopt;
}

// Dye color name -> numeric id (0..15).
std::optional<std::int32_t> DyeColorMap(std::string_view s) noexcept {
  if (s == "white") return 0;
  if (s == "orange") return 1;
  if (s == "magenta") return 2;
  if (s == "light_blue") return 3;
  if (s == "yellow") return 4;
  if (s == "lime") return 5;
  if (s == "pink") return 6;
  if (s == "gray") return 7;
  if (s == "light_gray") return 8;
  if (s == "cyan") return 9;
  if (s == "purple") return 10;
  if (s == "blue") return 11;
  if (s == "brown") return 12;
  if (s == "green") return 13;
  if (s == "red") return 14;
  if (s == "black") return 15;
  return std::nullopt;
}

// Primary tag-side field written by a rule kind, used for custom_data
// collision checks.
std::string_view PrimaryFieldForKind(ComponentKind kind) noexcept {
  switch (kind) {
    case ComponentKind::kText:
      return "display";  // display.Name / display.Lore
    case ComponentKind::kEnchantments:
      return "Enchantments";
    case ComponentKind::kStoredEnchantments:
      return "StoredEnchantments";
    case ComponentKind::kDamage:
      return "Damage";
    case ComponentKind::kMaxDamage:
      return "MaxDamage";
    case ComponentKind::kCustomModelData:
      return "CustomModelData";
    case ComponentKind::kUnbreakable:
      return "Unbreakable";
    case ComponentKind::kCharged:
      return "Charged";
    case ComponentKind::kChargedProjectiles:
      return "ChargedProjectiles";
    case ComponentKind::kDyedColor:
      return "display";
    case ComponentKind::kMapColor:
      return "display";
    case ComponentKind::kMapId:
      return "map";
    case ComponentKind::kPotionContents:
      return "Potion";
    case ComponentKind::kCustomData:
      return {};  // no single primary field
    case ComponentKind::kContainer:
      return "Items";
    case ComponentKind::kTooltipDisplay:
      return "HideFlags";
    case ComponentKind::kTrim:
      return "Trim";
    case ComponentKind::kFireworks:
      return "Fireworks";
    case ComponentKind::kFireworkExplosion:
      return "Explosion";
    case ComponentKind::kAttributeModifiers:
      return "AttributeModifiers";
    case ComponentKind::kCanPlaceOn:
      return "CanPlaceOn";
    case ComponentKind::kCanBreak:
      return "CanDestroy";
    case ComponentKind::kProfile:
      return "SkullOwner";
    case ComponentKind::kStewEffects:
      return "Effects";
    case ComponentKind::kBundleContents:
      return "Items";
    case ComponentKind::kWritableBook:
      return "pages";
    case ComponentKind::kWrittenBook:
      return "pages";
    case ComponentKind::kLodestoneTracker:
      return "LodestonePos";
    case ComponentKind::kDroppedKnown:
      return {};  // no target field
    case ComponentKind::kRenameToField:
      return {};  // rename has no single primary field
    case ComponentKind::kBannerPatterns:
      return {};  // written under BlockEntityTag; custom_data is at root
    case ComponentKind::kMapDecorations:
      return "Decorations";
    case ComponentKind::kAxolotlVariant:
      return "Variant";
    case ComponentKind::kTropicalFishData:
      return "BucketVariantTag";
  }
  return {};
}

bool ApplyComponent(RemapContext& ctx,
                    const ComponentRule& rule,
                    NbtTag& comp,
                    NbtCompound& item,
                    NbtCompound* be_tag,
                    std::string_view item_id,
                    std::deque<std::string>& scratch,
                    std::uint8_t& hide_bits,
                    std::string_view path) {
  const NbtCompound* body = AsCompound(comp);
  switch (rule.kind) {
    case ComponentKind::kText: {
      const bool is_item_name = rule.component == "minecraft:item_name";
      const bool is_name =
          rule.component == "minecraft:custom_name" || is_item_name;
      if (comp.type == TagType::kString) {
        if (is_item_name) {
          if (const NbtTag* d = FindChild(item, "display"); d != nullptr) {
            if (const NbtCompound* dc = AsCompound(*d);
                dc != nullptr && FindChild(*dc, "Name") != nullptr) {
              ++ctx.stats().counters.components_mapped;
              return true;
            }
          }
        }
        NbtCompound& display = EnsureCompound(item, "display");
        std::string_view payload = AsString(comp);
        if (ctx.src_dv() >= kDvComponents) {
          if (payload.empty() || payload.front() != '{') {
            scratch.push_back(WrapPlainTextAsJson(payload));
            payload = scratch.back();
            ++ctx.stats().counters.texts_downgraded;
            ctx.Report(Diag::kTextDowngraded, rule.rule_id, path);
          }
        }
        if (is_name) {
          RemoveChild(display, "Name");
          display.children.push_back(StringTag("Name", payload));
        } else {
          NbtList& lore = EnsureList(display, "Lore", TagType::kString);
          lore.children.push_back(NbtPayload{payload});
        }
        ++ctx.stats().counters.components_mapped;
        return true;
      }
      if (comp.type == TagType::kList || comp.type == TagType::kCompound) {
        if (comp.type == TagType::kCompound) {
          if (auto* cp =
                  std::get_if<std::unique_ptr<NbtCompound>>(&comp.payload)) {
            if (*cp) PreprocessTextHoverItems(ctx, **cp, scratch, path);
          }
        } else if (comp.type == TagType::kList) {
          if (auto* lp = std::get_if<std::unique_ptr<NbtList>>(&comp.payload)) {
            if (*lp) {
              for (auto& e : (*lp)->children) {
                if (auto* cp = std::get_if<std::unique_ptr<NbtCompound>>(&e)) {
                  if (*cp) PreprocessTextHoverItems(ctx, **cp, scratch, path);
                }
              }
            }
          }
        }

        NbtCompound& display = EnsureCompound(item, "display");
        if (is_name) {
          if (is_item_name && FindChild(display, "Name") != nullptr) {
            ++ctx.stats().counters.components_mapped;
            return true;
          }
          RemoveChild(display, "Name");
          if (auto json = StructuredTextToJson(comp.type, comp.payload)) {
            scratch.push_back(std::move(*json));
            display.children.push_back(StringTag("Name", scratch.back()));
            ++ctx.stats().counters.texts_downgraded;
          } else {
            ++ctx.stats().counters.text_unsupported_shapes;
            ctx.Report(Diag::kTextUnsupportedShape, rule.rule_id, path);
          }
        } else if (const NbtList* list = AsList(comp)) {
          NbtList& lore = EnsureList(display, "Lore", TagType::kString);
          for (const auto& e : list->children) {
            if (const auto* sv = std::get_if<std::string_view>(&e)) {
              if (ctx.src_dv() >= kDvComponents) {
                scratch.push_back(WrapPlainTextAsJson(*sv));
                lore.children.push_back(
                    NbtPayload{std::string_view(scratch.back())});
                ++ctx.stats().counters.texts_downgraded;
              } else {
                lore.children.push_back(NbtPayload{*sv});
              }
              continue;
            }
            if (auto json = StructuredTextToJson(list->element_type, e)) {
              scratch.push_back(std::move(*json));
              lore.children.push_back(
                  NbtPayload{std::string_view(scratch.back())});
              ++ctx.stats().counters.texts_downgraded;
            }
          }
        } else if (body != nullptr) {
          NbtList& lore = EnsureList(display, "Lore", TagType::kString);
          if (auto json = StructuredTextToJson(comp.type, comp.payload)) {
            scratch.push_back(std::move(*json));
            lore.children.push_back(
                NbtPayload{std::string_view(scratch.back())});
            ++ctx.stats().counters.texts_downgraded;
          }
        }
        ++ctx.stats().counters.components_mapped;
        return true;
      }
      return false;
    }
    case ComponentKind::kEnchantments:
    case ComponentKind::kStoredEnchantments: {
      const std::string_view out_field =
          rule.kind == ComponentKind::kStoredEnchantments ? "StoredEnchantments"
                                                          : "Enchantments";
      const NbtCompound* levels = body;
      std::optional<bool> show;
      if (body != nullptr) {
        if (const NbtTag* lv = FindChild(*body, "levels");
            lv != nullptr && AsCompound(*lv) != nullptr) {
          levels = AsCompound(*lv);
        }
        if (const NbtTag* st = FindChild(*body, "show_in_tooltip")) {
          show = BoolOf(*st);
        }
      }
      if (show.has_value() && !*show) hide_bits |= kHideEnchantments;
      if (levels == nullptr || levels->children.empty()) {
        ++ctx.stats().counters.components_mapped;
        return true;
      }
      NbtList& ench = EnsureList(item, out_field, TagType::kCompound);
      for (const auto& entry : levels->children) {
        if (entry.name == "levels" || entry.name == "show_in_tooltip") {
          continue;
        }
        const auto lvl = IntOf(entry);
        if (!lvl.has_value()) continue;
        NbtCompound& e = AppendListElementCompound(ench);
        e.children.push_back(StringTag("id", entry.name));
        std::int16_t narrowed;
        if (*lvl > 32767 || *lvl < -32768) {
          narrowed = static_cast<std::int16_t>(
              std::clamp<std::int64_t>(*lvl, -32768, 32767));
          ++ctx.stats().counters.enchant_level_clamps;
          ctx.Report(Diag::kEnchantmentLevelClamped, rule.rule_id, path);
        } else {
          narrowed = static_cast<std::int16_t>(*lvl);
        }
        e.children.push_back(MakeShortTag("lvl", narrowed));
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }
    case ComponentKind::kDamage:
    case ComponentKind::kMaxDamage:
    case ComponentKind::kMapColor: {
      if (comp.type != TagType::kInt) return false;
      const auto* v = std::get_if<std::int32_t>(&comp.payload);
      if (v == nullptr) return false;
      if (rule.kind == ComponentKind::kMapColor) {
        NbtCompound& display = EnsureCompound(item, "display");
        display.children.push_back(MakeIntTag("MapColor", *v));
      } else {
        const std::string_view field =
            rule.kind == ComponentKind::kDamage ? "Damage" : "MaxDamage";
        item.children.push_back(MakeIntTag(field, *v));
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }
    case ComponentKind::kCustomModelData: {
      if (comp.type == TagType::kInt) {
        const auto* v = std::get_if<std::int32_t>(&comp.payload);
        if (v == nullptr) return false;
        item.children.push_back(MakeIntTag("CustomModelData", *v));
        ++ctx.stats().counters.components_mapped;
        return true;
      }
      if (body != nullptr) {
        if (const NbtTag* floats = FindChild(*body, "floats")) {
          if (const NbtList* fl = AsList(*floats);
              fl != nullptr && !fl->children.empty()) {
            if (const auto* f = std::get_if<double>(&fl->children[0])) {
              item.children.push_back(
                  MakeIntTag("CustomModelData", static_cast<std::int32_t>(*f)));
              ctx.Report(Diag::kCustomModelDataCoerced, rule.rule_id, path);
              ++ctx.stats().counters.components_mapped;
              return true;
            }
          }
        }
      }
      return false;
    }
    case ComponentKind::kUnbreakable: {
      std::optional<bool> show;
      if (comp.type == TagType::kByte) {
        const auto* v = std::get_if<std::int8_t>(&comp.payload);
        if (v == nullptr) return false;
        item.children.push_back(MakeByteTag("Unbreakable", *v != 0 ? 1 : 0));
      } else if (body != nullptr) {
        item.children.push_back(MakeByteTag("Unbreakable", 1));
        if (const NbtTag* st = FindChild(*body, "show_in_tooltip")) {
          show = BoolOf(*st);
        }
      } else {
        return false;
      }
      if (show.has_value() && !*show) hide_bits |= kHideUnbreakable;
      ++ctx.stats().counters.components_mapped;
      return true;
    }
    case ComponentKind::kCharged: {
      item.children.push_back(MakeByteTag("Charged", 1));
      ++ctx.stats().counters.components_mapped;
      return true;
    }
    case ComponentKind::kDyedColor: {
      if (body == nullptr) return false;
      std::optional<bool> show;
      if (const NbtTag* st = FindChild(*body, "show_in_tooltip")) {
        show = BoolOf(*st);
      }
      if (const NbtTag* rgb = FindChild(*body, "rgb")) {
        if (const auto rgb_v = IntOf(*rgb)) {
          NbtCompound& display = EnsureCompound(item, "display");
          display.children.push_back(
              MakeIntTag("color", static_cast<std::int32_t>(*rgb_v)));
          if (show.has_value() && !*show) hide_bits |= kHideDyed;
          ++ctx.stats().counters.components_mapped;
          return true;
        }
      }
      return false;
    }
    case ComponentKind::kCustomData:
      return true;
    case ComponentKind::kContainer: {
      if (be_tag == nullptr) return false;
      NbtList* entries = AsListMutable(comp);
      if (entries == nullptr || entries->element_type != TagType::kCompound) {
        return false;
      }

      const bool is_decorated_pot =
          item_id.find("decorated_pot") != std::string_view::npos;

      if (is_decorated_pot) {
        if (ctx.tgt_dv() < 3681) {
          ctx.Report(Diag::kBeFieldNoTarget, rule.rule_id, path);
          ++ctx.stats().counters.components_dropped;
          return true;
        }

        for (auto& e : entries->children) {
          auto* entry_ptr = std::get_if<std::unique_ptr<NbtCompound>>(&e);
          if (entry_ptr == nullptr || *entry_ptr == nullptr) continue;
          for (auto& f : (*entry_ptr)->children) {
            if (f.name == "item" && f.type == TagType::kCompound) {
              auto* ip = std::get_if<std::unique_ptr<NbtCompound>>(&f.payload);
              if (ip != nullptr && *ip != nullptr) {
                DowngradeItemCompound(ctx, **ip, scratch, path);
                RemoveChild(*be_tag, "item");
                be_tag->children.push_back(
                    MakeCompoundTag("item", std::move(*ip)));
              }
              break;
            }
          }
          break;
        }
      } else {
        NbtList& items = EnsureList(*be_tag, "Items", TagType::kCompound);
        for (auto& e : entries->children) {
          auto* entry_ptr = std::get_if<std::unique_ptr<NbtCompound>>(&e);
          if (entry_ptr == nullptr || *entry_ptr == nullptr) continue;
          std::optional<std::int64_t> slot;
          std::unique_ptr<NbtCompound> item_body;
          for (auto& f : (*entry_ptr)->children) {
            if (f.name == "slot") slot = IntOf(f);
            if (f.name == "item" && f.type == TagType::kCompound) {
              auto* ip = std::get_if<std::unique_ptr<NbtCompound>>(&f.payload);
              if (ip != nullptr && *ip != nullptr) {
                item_body = std::move(*ip);
              }
            }
          }
          if (item_body == nullptr) continue;
          DowngradeItemCompound(ctx, *item_body, scratch, path);
          const std::int64_t s = slot.value_or(0);
          if (s < 0 || s > 127) {
            ++ctx.stats().counters.item_count_clamps;
            ctx.Report(Diag::kItemCountClamped, rule.rule_id, path);
            continue;
          }
          RemoveChild(*item_body, "Slot");
          item_body->children.push_back(
              MakeByteTag("Slot", static_cast<std::int8_t>(s)));
          ++ctx.stats().counters.slots_synthesized;
          items.children.push_back(NbtPayload{std::move(item_body)});
        }
      }

      if (!item_id.empty()) {
        RemoveChild(*be_tag, "id");
        be_tag->children.push_back(StringTag("id", item_id));
      }

      ++ctx.stats().counters.container_components;
      ctx.Report(Diag::kContainerToItems, rule.rule_id, path);
      ++ctx.stats().counters.components_mapped;
      return true;
    }
    case ComponentKind::kTooltipDisplay: {
      if (body == nullptr) return false;
      if (const NbtTag* ht = FindChild(*body, "hide_tooltip")) {
        if (BoolOf(*ht).value_or(false)) {
          hide_bits |= kHideAll;
          ++ctx.stats().counters.hideflags_folded;
        }
      }
      if (const NbtTag* hidden = FindChild(*body, "hidden_components")) {
        if (const NbtList* names = AsList(*hidden)) {
          for (const auto& e : names->children) {
            if (const auto* sv = std::get_if<std::string_view>(&e)) {
              hide_bits |= HideFlagForComponent(*sv);
            }
          }
        }
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kEntityData: {
      if (comp.type != TagType::kCompound) return false;
      auto* comp_ptr = std::get_if<std::unique_ptr<NbtCompound>>(&comp.payload);
      if (comp_ptr == nullptr || *comp_ptr == nullptr) return false;
      if (auto r = DowngradeEntityCompound(ctx, **comp_ptr, scratch, path);
          !r) {
        return false;
      }
      RemoveChild(item, "EntityTag");
      item.children.push_back(
          MakeCompoundTag("EntityTag", std::move(*comp_ptr)));
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kBlockEntityData: {
      if (be_tag == nullptr || comp.type != TagType::kCompound) return false;
      auto* comp_ptr = std::get_if<std::unique_ptr<NbtCompound>>(&comp.payload);
      if (comp_ptr == nullptr || *comp_ptr == nullptr) return false;
      if (auto r = DowngradeBlockEntityCompound(ctx, **comp_ptr, scratch, path);
          !r) {
        return false;
      }
      for (auto& f : (*comp_ptr)->children) {
        RemoveChild(*be_tag, f.name);
        be_tag->children.push_back(std::move(f));
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kBucketEntityData: {
      if (comp.type != TagType::kCompound) return false;
      auto* comp_ptr = std::get_if<std::unique_ptr<NbtCompound>>(&comp.payload);
      if (comp_ptr == nullptr || *comp_ptr == nullptr) return false;
      if (auto r = DowngradeEntityCompound(ctx, **comp_ptr, scratch, path);
          !r) {
        return false;
      }

      NbtCompound& entity_tag = EnsureCompound(item, "EntityTag");
      for (auto& f : (*comp_ptr)->children) {
        RemoveChild(entity_tag, f.name);
        entity_tag.children.push_back(std::move(f));
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kBees: {
      if (be_tag == nullptr || comp.type != TagType::kList) return false;
      NbtList* entries = AsListMutable(comp);
      if (entries == nullptr || entries->element_type != TagType::kCompound) {
        return false;
      }
      NbtList& out = EnsureList(*be_tag, "Bees", TagType::kCompound);
      WriteBeesListInto(ctx, out, *entries, rule.rule_id, scratch, path);
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kContainerLoot: {
      if (be_tag == nullptr || body == nullptr) return false;
      if (const NbtTag* lt = FindChild(*body, "loot_table")) {
        if (lt->type == TagType::kString) {
          RemoveChild(*be_tag, "LootTable");
          be_tag->children.push_back(StringTag("LootTable", AsString(*lt)));
        } else if (lt->type == TagType::kCompound) {
          const NbtCompound* ltc = AsCompound(*lt);
          const NbtTag* type_tag =
              ltc != nullptr ? FindChild(*ltc, "type") : nullptr;
          if (type_tag != nullptr && type_tag->type == TagType::kString) {
            RemoveChild(*be_tag, "LootTable");
            be_tag->children.push_back(
                StringTag("LootTable", AsString(*type_tag)));
            ctx.Report(Diag::kPredicateDowngraded, rule.rule_id, path);
          } else {
            ctx.Report(Diag::kFallbackPassthrough, rule.rule_id, path);
          }
        }
      }
      if (const NbtTag* seed = FindChild(*body, "seed"); seed != nullptr) {
        std::optional<std::int64_t> v;
        if (seed->type == TagType::kLong) {
          if (const auto* lp = std::get_if<std::int64_t>(&seed->payload)) {
            v = *lp;
          }
        } else if (const auto i = IntOf(*seed)) {
          v = i;
        }
        if (v.has_value()) {
          RemoveChild(*be_tag, "LootTableSeed");
          be_tag->children.push_back(MakeLongTag("LootTableSeed", *v));
        }
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kPotDecorations: {
      if (be_tag == nullptr) return false;
      RemoveChild(*be_tag, "sherds");
      NbtTag sherds;
      sherds.type = comp.type;
      sherds.name = "sherds";
      sherds.payload = std::move(comp.payload);
      be_tag->children.push_back(std::move(sherds));
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kBannerPatterns: {
      if (be_tag == nullptr || comp.type != TagType::kList) return false;
      NbtList* entries = AsListMutable(comp);
      if (entries == nullptr || entries->element_type != TagType::kCompound) {
        return false;
      }

      RemoveChild(*be_tag, "id");
      be_tag->children.push_back(StringTag("id", "minecraft:banner"));

      RemoveChild(*be_tag, "Patterns");
      NbtList& out = EnsureList(*be_tag, "Patterns", TagType::kCompound);
      WriteBannerPatternsListInto(ctx, out, *entries, rule.rule_id, path);
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kMapDecorations: {
      if (body == nullptr) return false;

      RemoveChild(item, "Decorations");
      NbtList& out = EnsureList(item, "Decorations", TagType::kCompound);
      const auto& type_map = GetDecorationTypeMap();
      for (const auto& f : body->children) {
        if (f.type != TagType::kCompound) continue;
        const NbtCompound* entry = AsCompound(f);
        if (entry == nullptr) continue;

        NbtCompound& e = AppendListElementCompound(out);

        e.children.push_back(StringTag("id", f.name));

        double x = 0.0;
        if (const NbtTag* xt = FindChild(*entry, "x"); xt != nullptr) {
          if (const auto v = FloatOf(*xt)) x = *v;
        }
        double z = 0.0;
        if (const NbtTag* zt = FindChild(*entry, "z"); zt != nullptr) {
          if (const auto v = FloatOf(*zt)) z = *v;
        }
        double rot = 0.0;
        if (const NbtTag* rt = FindChild(*entry, "rotation"); rt != nullptr) {
          if (const auto v = FloatOf(*rt)) rot = *v;
        }
        e.children.push_back(MakeDoubleTag("x", x));
        e.children.push_back(MakeDoubleTag("z", z));
        e.children.push_back(MakeDoubleTag("rot", rot));

        std::int8_t type = 0;
        if (const NbtTag* tt = FindChild(*entry, "type"); tt != nullptr) {
          const std::string_view s = AsString(*tt);
          if (auto it = type_map.find(s); it != type_map.end()) {
            type = it->second;
          } else if (!s.empty()) {
            ctx.Report(Diag::kBeFieldNoTarget, rule.rule_id, path);
          }
        }
        e.children.push_back(MakeByteTag("type", type));
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kAxolotlVariant: {
      if (comp.type != TagType::kString) return false;
      const std::string_view s = AsString(comp);
      if (auto v = AxolotlVariantMap(s)) {
        item.children.push_back(MakeIntTag("Variant", *v));
        ++ctx.stats().counters.components_mapped;
        return true;
      }
      return false;
    }
    case ComponentKind::kTropicalFishData: {
      if (comp.type != TagType::kString) return false;
      const std::string_view s = AsString(comp);

      std::int32_t mask = 0;
      std::int32_t shift = 0;
      if (rule.component == "minecraft:tropical_fish/pattern") {
        auto v = TropicalFishPatternMap(s);
        if (!v) return false;
        mask = *v;
        shift = 0;
      } else if (rule.component == "minecraft:tropical_fish/base_color") {
        auto v = DyeColorMap(s);
        mask = v ? *v : 0;
        shift = 16;
      } else if (rule.component == "minecraft:tropical_fish/pattern_color") {
        auto v = DyeColorMap(s);
        mask = v ? *v : 0;
        shift = 24;
      }

      NbtTag* tag = FindChild(item, "BucketVariantTag");
      std::int32_t current = 0;
      if (tag && tag->type == TagType::kInt) {
        if (const auto* p = std::get_if<std::int32_t>(&tag->payload))
          current = *p;
      } else {
        item.children.push_back(MakeIntTag("BucketVariantTag", 0));
      }

      std::int32_t new_val = current | (mask << shift);
      RemoveChild(item, "BucketVariantTag");
      item.children.push_back(MakeIntTag("BucketVariantTag", new_val));

      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kRenameToField: {
      if (rule.target.empty()) return false;
      NbtCompound* dst = nullptr;
      switch (rule.rename_target) {
        case RenameTarget::kItemRoot:
          dst = &item;
          break;
        case RenameTarget::kDisplay:
          dst = &EnsureCompound(item, "display");
          break;
        case RenameTarget::kBlockEntityTag:
          if (be_tag == nullptr) return false;
          dst = be_tag;
          break;
      }
      if (dst == nullptr) return false;
      RemoveChild(*dst, rule.target);
      NbtTag moved;
      moved.type = comp.type;
      moved.name = rule.target;
      moved.payload = std::move(comp.payload);
      dst->children.push_back(std::move(moved));
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    default:
      break;
  }
  return ApplyComponentExtra(ctx, rule, comp, item, scratch, hide_bits, path);
}

}  // namespace

void DowngradeItemCompound(RemapContext& ctx,
                           NbtCompound& item,
                           std::deque<std::string>& scratch,
                           std::string_view path) {
  ++ctx.stats().counters.items_rewritten;

  if (ctx.IdRenameFamilyActive()) {
    if (NbtTag* id = FindChild(item, "id"); id != nullptr) {
      const std::string_view cur = AsString(*id);
      if (const IdRenameRule* r =
              FindIdRename(cur, ctx.tgt_dv(), ctx.src_dv())) {
        id->payload = r->older_id;
        ++ctx.stats().counters.item_ids_renamed;
        ctx.Report(Diag::kItemIdRenamed, r->rule_id, path);
      }
    }
  }

  if (NbtTag* count = FindChild(item, "count");
      count != nullptr && count->type == TagType::kInt) {
    if (const auto* v = std::get_if<std::int32_t>(&count->payload);
        v != nullptr) {
      std::int64_t c = *v;
      if (c > 127 || c < 0) {
        ++ctx.stats().counters.item_count_clamps;
        ctx.Report(Diag::kItemCountClamped, 0, path);
        c = std::clamp<std::int64_t>(c, 0, 127);
      }
      item.children.push_back(
          MakeByteTag("Count", static_cast<std::int8_t>(c)));
    }
    RemoveChild(item, "count");
  }

  std::unique_ptr<NbtCompound> comps = TakeCompoundChild(item, "components");
  if (comps == nullptr) {
    return;
  }
  ctx.Report(Diag::kItemShellRewritten, 0, path);

  auto tag = std::make_unique<NbtCompound>();
  auto be_tag = std::make_unique<NbtCompound>();

  std::unique_ptr<NbtCompound> custom_data =
      TakeCompoundChild(*comps, "minecraft:custom_data");
  if (custom_data != nullptr) {
    for (auto& f : custom_data->children) {
      tag->children.push_back(std::move(f));
    }
  }

  std::string_view item_id;
  if (const NbtTag* id_tag = FindChild(item, "id"); id_tag != nullptr) {
    item_id = AsString(*id_tag);
  }
  bool has_custom_name = false;
  for (const auto& c : comps->children) {
    if (c.name == "minecraft:custom_name") {
      has_custom_name = true;
      break;
    }
  }
  std::uint8_t hide_bits = 0;
  for (auto& c : comps->children) {
    if (c.name == "minecraft:item_name" && has_custom_name) {
      ++ctx.stats().counters.components_dropped;
      ctx.Report(Diag::kComponentDropped, 0, path);
      continue;
    }

    const ComponentRule* rule = FindComponentRule(c.name);
    if (rule == nullptr) {
      const std::string comp_path =
          std::string(path) + ".components." + std::string(c.name);
      switch (ctx.options().unmapped_components) {
        case RemapOptions::UnmappedComponentPolicy::kDrop:
          ++ctx.stats().counters.components_dropped;
          ctx.Report(Diag::kComponentDropped, 0, comp_path);
          break;
        case RemapOptions::UnmappedComponentPolicy::kMergeIntoTag: {
          ++ctx.stats().counters.components_merged_verbatim;
          ctx.Report(Diag::kComponentMergedVerbatim, 0, comp_path);
          if (auto* body = AsCompoundMutable(c)) {
            auto keep = std::make_unique<NbtCompound>(std::move(*body));
            tag->children.push_back(MakeCompoundTag(c.name, std::move(keep)));
          }
          break;
        }
      }
      continue;
    }
    if (rule->kind == ComponentKind::kCustomData) continue;
    if (rule->kind == ComponentKind::kDroppedKnown) {
      ++ctx.stats().counters.components_no_target;
      const std::string comp_path =
          std::string(path) + ".components." + std::string(c.name);
      ctx.Report(Diag::kComponentNoTarget, rule->rule_id, comp_path);
      if (ctx.options().unmapped_components ==
          RemapOptions::UnmappedComponentPolicy::kMergeIntoTag) {
        if (auto* body = AsCompoundMutable(c)) {
          auto keep = std::make_unique<NbtCompound>(std::move(*body));
          tag->children.push_back(MakeCompoundTag(c.name, std::move(keep)));
        }
      } else {
        ++ctx.stats().counters.components_dropped;
      }
      continue;
    }
    if (custom_data != nullptr) {
      const std::string_view primary = PrimaryFieldForKind(rule->kind);
      if (!primary.empty() && FindChild(*custom_data, primary) != nullptr) {
        ++ctx.stats().counters.custom_data_collisions;
        ctx.Report(Diag::kCustomDataCollision, rule->rule_id, path);
      }
    }
    if (!ApplyComponent(ctx,
                        *rule,
                        c,
                        *tag,
                        be_tag.get(),
                        item_id,
                        scratch,
                        hide_bits,
                        path)) {
      ++ctx.stats().counters.components_dropped;
      const std::string comp_path =
          std::string(path) + ".components." + std::string(c.name);
      ctx.Report(Diag::kComponentDropped, rule->rule_id, comp_path);
    }
  }

  if (hide_bits != 0) {
    AccumulateHideFlags(*tag, hide_bits);
    ++ctx.stats().counters.hideflags_folded;
    ctx.Report(Diag::kHideFlagsFolded, 0, path);
  }

  if (!be_tag->children.empty()) {
    if (FindChild(*be_tag, "id") == nullptr) {
      if (const NbtTag* id_tag = FindChild(item, "id")) {
        const std::string_view item_id_auto = AsString(*id_tag);
        constexpr std::string_view kBucketSuffix = "_bucket";
        if (!item_id_auto.ends_with(kBucketSuffix)) {
          be_tag->children.push_back(StringTag("id", item_id_auto));
        }
      }
    }
    tag->children.push_back(
        MakeCompoundTag("BlockEntityTag", std::move(be_tag)));
  }
  if (!tag->children.empty()) {
    item.children.push_back(MakeCompoundTag("tag", std::move(tag)));
  }
}

}  // namespace fschema::remap::internal