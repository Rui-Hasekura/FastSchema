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

// FastSchema remap: extra component -> tag transforms.
//
// Reached from ApplyComponent's default branch in item.cc; kDroppedKnown
// rows are intercepted earlier in the component loop and never reach this
// file.
#include "fschema/remap/internal/component_extra.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "fschema/remap/internal/context.h"
#include "fschema/remap/internal/item.h"
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

// Reads either a string field or a {id: "minecraft:x"} compound field.
std::string_view IdOfStringOrCompound(const NbtCompound& body,
                                      std::string_view field) noexcept {
  const NbtTag* t = FindChild(body, field);
  if (t == nullptr) return {};
  if (const std::string_view s = AsString(*t); !s.empty()) return s;
  if (const NbtCompound* c = AsCompound(*t)) {
    if (const NbtTag* id = FindChild(*c, "id")) return AsString(*id);
  }
  return {};
}

// `color` / `fade_color` are a single int, an IntArray, or a list of ints.
void CollectIntColors(const NbtTag& t, std::vector<std::int32_t>& out) {
  if (t.type == TagType::kInt) {
    if (const auto* v = std::get_if<std::int32_t>(&t.payload)) {
      out.push_back(*v);
    }
    return;
  }
  if (t.type == TagType::kIntArray) {
    if (const auto* raw = std::get_if<std::span<const std::byte>>(&t.payload)) {
      for (std::int32_t v : ReadIntArray(*raw)) out.push_back(v);
    }
    return;
  }
  if (const NbtList* l = AsList(t)) {
    for (const auto& e : l->children) {
      if (const auto* v = std::get_if<std::int32_t>(&e)) out.push_back(*v);
    }
  }
}

// Fills one target-era `Explosion` compound from a firework_explosion body.
void FillExplosion(RemapContext& ctx,
                   const NbtCompound& src,
                   NbtCompound& ex,
                   const ComponentRule& rule,
                   std::string_view path,
                   std::deque<std::string>& scratch) {
  std::int8_t type = 0;
  if (const NbtTag* shape = FindChild(src, "shape")) {
    const std::string_view s = AsString(*shape);
    if (s == "small_ball") {
      type = 0;
    } else if (s == "large_ball") {
      type = 1;
    } else if (s == "star") {
      type = 2;
    } else if (s == "creeper") {
      type = 3;
    } else if (s == "burst") {
      type = 4;
    } else {
      ctx.Report(Diag::kFireworkShapeUnknown, rule.rule_id, path);
    }
  }
  ex.children.push_back(MakeByteTag("Type", type));
  std::vector<std::int32_t> colors;
  if (const NbtTag* c = FindChild(src, "color")) CollectIntColors(*c, colors);
  if (!colors.empty()) {
    ex.children.push_back(
        MakeIntArrayTag("Colors", colors.data(), colors.size(), scratch));
  }
  std::vector<std::int32_t> fades;
  if (const NbtTag* c = FindChild(src, "fade_color")) {
    CollectIntColors(*c, fades);
  }
  if (!fades.empty()) {
    ex.children.push_back(
        MakeIntArrayTag("FadeColors", fades.data(), fades.size(), scratch));
  }
  if (const NbtTag* t = FindChild(src, "has_trail")) {
    if (BoolOf(*t).value_or(false)) {
      ex.children.push_back(MakeByteTag("Trail", 1));
    }
  }
  if (const NbtTag* t = FindChild(src, "has_twinkle")) {
    if (BoolOf(*t).value_or(false)) {
      ex.children.push_back(MakeByteTag("Flicker", 1));
    }
  }
}

// Maps a source operation name to the target-era numeric id.
std::optional<std::int32_t> MapOperation(std::string_view op) noexcept {
  if (op == "add_value") return 0;
  if (op == "multiply_base" || op == "add_multiplied_base") return 1;
  if (op == "multiply_total" || op == "add_multiplied_total") return 2;
  return std::nullopt;
}

// Target-era slot values are a subset of the source enum; hand / armor /
// body / saddle broaden to "any", the closest target-era semantics.
std::optional<std::string_view> MapAttrSlot(std::string_view slot) noexcept {
  if (slot == "any" || slot == "mainhand" || slot == "offhand" ||
      slot == "head" || slot == "chest" || slot == "legs" || slot == "feet") {
    return slot;
  }
  if (slot == "hand" || slot == "armor" || slot == "body" || slot == "saddle") {
    return std::string_view{"any"};
  }
  return std::nullopt;
}

// Returns true when the attribute id already carries a legacy prefix
// (minecraft:generic. / player. / zombie. / horse.).
bool HasLegacyAttrPrefix(std::string_view id) noexcept {
  constexpr std::string_view kGeneric = "minecraft:generic.";
  constexpr std::string_view kPlayer = "minecraft:player.";
  constexpr std::string_view kZombie = "minecraft:zombie.";
  constexpr std::string_view kHorse = "minecraft:horse.";
  return id.substr(0, kGeneric.size()) == kGeneric ||
         id.substr(0, kPlayer.size()) == kPlayer ||
         id.substr(0, kZombie.size()) == kZombie ||
         id.substr(0, kHorse.size()) == kHorse;
}

// Rename resolution shared between item component modifiers and entity
// Attributes entries. Returns the target-era name, or an empty view when
// the entry must be dropped. `kept_verbatim` is set when the id should
// pass through unchanged (legacy form or unknown id).
std::string_view ResolveAttrName(RemapContext& ctx,
                                 std::string_view type,
                                 std::string_view path,
                                 bool& kept_verbatim,
                                 bool& dropped_no_target) noexcept {
  kept_verbatim = false;
  dropped_no_target = false;
  if (const AttributeRenameRule* r =
          FindAttributeRename(type, ctx.tgt_dv(), ctx.src_dv())) {
    if (r->older_id.empty()) {
      dropped_no_target = true;
      ++ctx.stats().counters.attribute_no_target;
      ctx.Report(Diag::kAttributeNoTarget, r->rule_id, path);
      return {};
    }
    if (r->older_id == "minecraft:generic.scale" && ctx.tgt_dv() < 3801) {
      dropped_no_target = true;
      ++ctx.stats().counters.attribute_no_target;
      ctx.Report(Diag::kAttributeNoTarget, r->rule_id, path);
      return {};
    }
    ++ctx.stats().counters.attribute_renames;
    ctx.Report(Diag::kAttributeRenamed, r->rule_id, path);
    return r->older_id;
  }
  if (!HasLegacyAttrPrefix(type)) {
    ++ctx.stats().counters.attribute_no_target;
    ctx.Report(Diag::kAttributeNoTarget, 0, path);
  }
  kept_verbatim = true;
  return type;
}

}  // namespace

// Builds a target-era `SkullOwner` compound from a {name, id, properties}
// profile body and appends it to `out_parent` under `field_name`.
bool WriteSkullOwner(RemapContext& ctx,
                     const base::NbtTag& src,
                     base::NbtCompound& out_parent,
                     std::string_view field_name,
                     std::deque<std::string>& scratch,
                     std::uint16_t rule_id,
                     std::string_view path) {
  const NbtCompound* body = AsCompound(src);
  if (body == nullptr) return false;

  std::string_view name;
  if (const NbtTag* nt = FindChild(*body, "name")) name = AsString(*nt);

  std::optional<std::array<std::int32_t, 4>> uuid;
  if (const NbtTag* idt = FindChild(*body, "id")) {
    if (idt->type == TagType::kIntArray) {
      if (const auto* raw =
              std::get_if<std::span<const std::byte>>(&idt->payload)) {
        const std::vector<std::int32_t> ints = ReadIntArray(*raw);
        if (ints.size() == 4) {
          uuid =
              std::array<std::int32_t, 4>{ints[0], ints[1], ints[2], ints[3]};
        } else {
          ctx.Report(Diag::kProfileIdDropped, rule_id, path);
        }
      } else {
        ctx.Report(Diag::kProfileIdDropped, rule_id, path);
      }
    } else if (idt->type == TagType::kString) {
      const std::string_view id = AsString(*idt);
      if (!id.empty()) {
        uuid = ParseUuidIntArray(id);
        if (!uuid.has_value()) {
          ctx.Report(Diag::kProfileIdDropped, rule_id, path);
        }
      }
    }
  }

  NbtCompound& skull = EnsureCompound(out_parent, field_name);
  if (!name.empty()) {
    skull.children.push_back(StringTag("Name", name));
  }
  if (uuid.has_value()) {
    skull.children.push_back(
        MakeIntArrayTag("Id", uuid->data(), uuid->size(), scratch));
  }

  const NbtTag* pt = FindChild(*body, "properties");
  if (pt == nullptr) return true;

  if (const NbtList* list = AsList(*pt)) {
    NbtList* textures_list = nullptr;
    for (const auto& e : list->children) {
      const auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
      if (ep == nullptr || *ep == nullptr) continue;
      const NbtCompound& src_prop = **ep;
      std::string_view key;
      if (const NbtTag* k = FindChild(src_prop, "name")) {
        key = AsString(*k);
      }
      std::string_view value;
      if (const NbtTag* v = FindChild(src_prop, "value")) {
        value = AsString(*v);
      }
      if (key != "textures" || value.empty()) continue;

      if (textures_list == nullptr) {
        NbtCompound& properties_cpd = EnsureCompound(skull, "Properties");
        NbtList& tl =
            EnsureList(properties_cpd, "textures", TagType::kCompound);
        textures_list = &tl;
      }
      NbtCompound& o = AppendListElementCompound(*textures_list);
      o.children.push_back(StringTag("Value", value));
      if (const NbtTag* sig = FindChild(src_prop, "signature")) {
        const std::string_view s = AsString(*sig);
        if (!s.empty()) {
          o.children.push_back(StringTag("Signature", s));
        }
      }
    }
  } else if (const NbtCompound* cpd = AsCompound(*pt)) {
    if (const NbtTag* textures_tag = FindChild(*cpd, "textures")) {
      if (const NbtList* text_list = AsList(*textures_tag)) {
        NbtList* textures_list = nullptr;
        for (const auto& e : text_list->children) {
          if (const auto* sv = std::get_if<std::string_view>(&e)) {
            if (textures_list == nullptr) {
              NbtCompound& properties_cpd = EnsureCompound(skull, "Properties");
              NbtList& tl_ref =
                  EnsureList(properties_cpd, "textures", TagType::kCompound);
              textures_list = &tl_ref;
            }
            NbtCompound& o = AppendListElementCompound(*textures_list);
            o.children.push_back(StringTag("Value", *sv));
          }
        }
      }
    }
  }
  return true;
}

bool ApplyComponentExtra(RemapContext& ctx,
                         const ComponentRule& rule,
                         NbtTag& comp,
                         NbtCompound& item,
                         std::deque<std::string>& scratch,
                         std::uint8_t& hide_bits,
                         std::string_view path) {
  const NbtCompound* body = AsCompound(comp);

  switch (rule.kind) {
    case ComponentKind::kTrim: {
      if (body == nullptr) return false;
      const std::string_view pattern = IdOfStringOrCompound(*body, "pattern");
      const std::string_view material = IdOfStringOrCompound(*body, "material");
      if (pattern.empty() || material.empty()) return false;
      NbtCompound& trim = EnsureCompound(item, "Trim");
      trim.children.push_back(StringTag("pattern", pattern));
      trim.children.push_back(StringTag("material", material));
      if (const NbtTag* st = FindChild(*body, "show_in_tooltip")) {
        if (!BoolOf(*st).value_or(true)) {
          hide_bits |= kHideAdditional;
        }
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kFireworks: {
      if (body == nullptr) return false;
      NbtCompound& fw = EnsureCompound(item, "Fireworks");

      std::int64_t flight_dur = 0;
      bool has_flight = false;
      if (const NbtTag* fd = FindChild(*body, "flight_duration")) {
        if (const auto v = IntOf(*fd)) {
          flight_dur = *v;
          has_flight = true;
        } else if (fd->type == TagType::kLong) {
          if (const auto* lp = std::get_if<std::int64_t>(&fd->payload)) {
            flight_dur = *lp;
            has_flight = true;
          }
        }
      }

      const std::int64_t clamped = std::clamp<std::int64_t>(flight_dur, 0, 255);
      if (clamped != flight_dur) {
        ctx.Report(Diag::kFireworkFlightClamped, rule.rule_id, path);
      }
      fw.children.push_back(
          MakeByteTag("Flight", static_cast<std::int8_t>(clamped)));

      if (!has_flight) {
        ctx.Report(Diag::kFieldMissing,
                   rule.rule_id,
                   std::string(path) +
                       ".components.minecraft:fireworks.flight_duration");
      }
      if (const NbtTag* ex = FindChild(*body, "explosions")) {
        if (const NbtList* list = AsList(*ex)) {
          NbtList& explosions =
              EnsureList(fw, "Explosions", TagType::kCompound);
          for (const auto& e : list->children) {
            const auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
            if (ep == nullptr || *ep == nullptr) continue;
            NbtCompound& target = AppendListElementCompound(explosions);
            FillExplosion(ctx, **ep, target, rule, path, scratch);
          }
        }
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kFireworkExplosion: {
      if (body == nullptr) return false;
      NbtCompound& ex = EnsureCompound(item, "Explosion");
      FillExplosion(ctx, *body, ex, rule, path, scratch);
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kAttributeModifiers: {
      const NbtList* mods = nullptr;
      std::optional<bool> show;
      if (comp.type == TagType::kList) {
        mods = AsList(comp);  // bare list form
      } else if (body != nullptr) {
        if (const NbtTag* m = FindChild(*body, "modifiers")) mods = AsList(*m);
        if (const NbtTag* st = FindChild(*body, "show_in_tooltip")) {
          show = BoolOf(*st);
        }
      }
      if (mods == nullptr) return false;
      if (show.has_value() && !*show) hide_bits |= kHideAttributeModifiers;
      NbtList& out = EnsureList(item, "AttributeModifiers", TagType::kCompound);
      std::size_t index = 0;
      for (const auto& e : mods->children) {
        const auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
        if (ep == nullptr || *ep == nullptr) continue;
        const NbtCompound& m = **ep;
        ++index;

        std::string_view type;
        if (const NbtTag* t = FindChild(m, "type")) type = AsString(*t);
        if (type.empty()) {
          if (const NbtTag* id = FindChild(m, "id")) type = AsString(*id);
        }
        bool kept_verbatim = false;
        bool dropped_no_target = false;
        const std::string_view attr_name =
            ResolveAttrName(ctx, type, path, kept_verbatim, dropped_no_target);
        if (dropped_no_target) {
          ++ctx.stats().counters.attr_modifiers_dropped;
          ctx.Report(Diag::kAttrModifierDropped, rule.rule_id, path);
          continue;
        }

        std::optional<std::int32_t> op;
        if (const NbtTag* ot = FindChild(m, "operation")) {
          if (const std::string_view s = AsString(*ot); !s.empty()) {
            op = MapOperation(s);
          } else if (const auto v = IntOf(*ot)) {
            if (*v >= 0 && *v <= 2) op = static_cast<std::int32_t>(*v);
          }
        }
        if (!op.has_value()) {
          ++ctx.stats().counters.attr_modifiers_dropped;
          ctx.Report(Diag::kAttrModifierDropped, rule.rule_id, path);
          continue;
        }

        double amount = 0.0;
        if (const NbtTag* at = FindChild(m, "amount")) {
          if (const auto f = FloatOf(*at)) {
            amount = *f;
          } else if (const auto i = IntOf(*at)) {
            amount = static_cast<double>(*i);
          }
        }

        // id: UUID string -> IntArray; arbitrary string -> deterministic
        // hash; absent -> synthesized from type + position.
        std::string_view id_str;
        if (const NbtTag* it = FindChild(m, "id")) id_str = AsString(*it);
        std::array<std::int32_t, 4> uuid{};
        if (!id_str.empty()) {
          if (auto parsed = ParseUuidIntArray(id_str)) {
            uuid = *parsed;
          } else {
            uuid = HashUuidIntArray(id_str);
            ++ctx.stats().counters.attr_ids_hashed;
            ctx.Report(Diag::kAttrIdHashed, rule.rule_id, path);
          }
        } else {
          std::string synth;
          synth.reserve(type.size() + 12);
          synth += type;
          synth += '#';
          synth += std::to_string(index);
          uuid = HashUuidIntArray(synth);
          ++ctx.stats().counters.attr_ids_hashed;
          ctx.Report(Diag::kAttrIdHashed, rule.rule_id, path);
        }

        std::string_view slot_out;
        bool broadened = false;
        if (const NbtTag* st = FindChild(m, "slot")) {
          const std::string_view s = AsString(*st);
          if (!s.empty()) {
            const auto mapped = MapAttrSlot(s);
            if (!mapped.has_value()) {
              ++ctx.stats().counters.attr_modifiers_dropped;
              ctx.Report(Diag::kAttrModifierDropped, rule.rule_id, path);
              continue;
            }
            broadened = *mapped != s;
            slot_out = *mapped;
          }
        }
        if (broadened) {
          ctx.Report(Diag::kAttrSlotBroadened, rule.rule_id, path);
        }
        if (FindChild(m, "display") != nullptr) {
          ++ctx.stats().counters.attr_modifier_displays_dropped;
          ctx.Report(Diag::kComponentDropped,
                     rule.rule_id,
                     std::string(path) +
                         ".components.minecraft:attribute_modifiers.display");
        }

        NbtCompound& out_e = AppendListElementCompound(out);
        out_e.children.push_back(StringTag("AttributeName", attr_name));
        out_e.children.push_back(
            StringTag("Name", id_str.empty() ? attr_name : id_str));
        out_e.children.push_back(MakeDoubleTag("Amount", amount));
        out_e.children.push_back(MakeIntTag("Operation", *op));
        out_e.children.push_back(
            MakeIntArrayTag("UUID", uuid.data(), uuid.size(), scratch));
        if (!slot_out.empty() && slot_out != "any") {
          out_e.children.push_back(StringTag("Slot", slot_out));
        }
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kCanPlaceOn:
    case ComponentKind::kCanBreak: {
      const bool is_place = rule.kind == ComponentKind::kCanPlaceOn;
      const NbtList* preds = nullptr;
      std::optional<bool> show;
      if (comp.type == TagType::kList) {
        preds = AsList(comp);
      } else if (body != nullptr) {
        if (const NbtTag* p = FindChild(*body, "predicates")) {
          preds = AsList(*p);
        }
        if (const NbtTag* st = FindChild(*body, "show_in_tooltip")) {
          show = BoolOf(*st);
        }
      }
      if (preds == nullptr) return false;
      if (show.has_value() && !*show) {
        hide_bits |= is_place ? kHideCanPlaceOn : kHideCanDestroy;
      }
      NbtList& out = EnsureList(
          item, is_place ? "CanPlaceOn" : "CanDestroy", TagType::kString);
      std::size_t pred_idx = 0;
      for (const auto& e : preds->children) {
        const auto* pp = std::get_if<std::unique_ptr<NbtCompound>>(&e);
        if (pp == nullptr || *pp == nullptr) continue;
        const NbtCompound& p = **pp;

        std::string exact_path = std::string(path) + ".components." +
                                 std::string(rule.component) + ".predicates[" +
                                 std::to_string(pred_idx) + "]";
        ++pred_idx;

        bool lossy = false;
        if (const NbtTag* blocks = FindChild(p, "blocks")) {
          if (const NbtList* bl = AsList(*blocks)) {
            for (const auto& b : bl->children) {
              if (const auto* sv = std::get_if<std::string_view>(&b)) {
                if (sv->empty()) continue;
                // Target-era CanDestroy / CanPlaceOn accept block tags
                // verbatim (`#namespace:tag`); keep them as-is.
                out.children.push_back(NbtPayload{*sv});
              }
            }
          } else if (const std::string_view single = AsString(*blocks);
                     !single.empty()) {
            out.children.push_back(NbtPayload{single});
          }
        } else {
          lossy = true;  // predicate without a blocks list: nothing emittable
        }

        if (FindChild(p, "state") != nullptr) {
          ++ctx.stats().counters.predicates_downgraded;
          ctx.Report(
              Diag::kPredicateDowngraded, rule.rule_id, exact_path + ".state");
        }
        if (FindChild(p, "nbt") != nullptr) {
          ++ctx.stats().counters.predicates_downgraded;
          ctx.Report(
              Diag::kPredicateDowngraded, rule.rule_id, exact_path + ".nbt");
        }
        if (FindChild(p, "components") != nullptr) {
          ++ctx.stats().counters.predicates_downgraded;
          ctx.Report(Diag::kPredicateDowngraded,
                     rule.rule_id,
                     exact_path + ".components");
        }

        if (lossy && FindChild(p, "state") == nullptr &&
            FindChild(p, "nbt") == nullptr &&
            FindChild(p, "components") == nullptr) {
          ++ctx.stats().counters.predicates_downgraded;
          ctx.Report(Diag::kPredicateDowngraded, rule.rule_id, exact_path);
        }
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kPotionContents: {
      if (body == nullptr) return false;
      bool any = false;
      if (const NbtTag* potion = FindChild(*body, "potion")) {
        const std::string_view id = AsString(*potion);
        if (!id.empty()) {
          item.children.push_back(StringTag("Potion", id));
          any = true;
        }
      }
      if (const NbtTag* cc = FindChild(*body, "custom_color")) {
        if (const auto v = IntOf(*cc)) {
          item.children.push_back(
              MakeIntTag("CustomPotionColor", static_cast<std::int32_t>(*v)));
          any = true;
        }
      }
      if (const NbtTag* ce = FindChild(*body, "custom_effects")) {
        if (const NbtList* list = AsList(*ce)) {
          NbtList& out =
              EnsureList(item, "custom_potion_effects", TagType::kCompound);
          for (const auto& e : list->children) {
            const auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
            if (ep == nullptr || *ep == nullptr) continue;
            const NbtCompound& fx = **ep;
            std::string_view fx_id;
            if (const NbtTag* idt = FindChild(fx, "id")) fx_id = AsString(*idt);
            const PotionEffectRule* r =
                fx_id.empty() ? nullptr : FindPotionEffectId(fx_id);
            if (r == nullptr) {
              ++ctx.stats().counters.effects_dropped;
              ctx.Report(Diag::kEffectNoTarget, rule.rule_id, path);
              continue;
            }
            NbtCompound& o = AppendListElementCompound(out);
            o.children.push_back(
                MakeByteTag("Id", static_cast<std::int8_t>(r->numeric)));
            if (const NbtTag* a = FindChild(fx, "amplifier")) {
              if (const auto v = IntOf(*a); v.has_value() && *v != 0) {
                o.children.push_back(
                    MakeByteTag("Amplifier",
                                static_cast<std::int8_t>(
                                    std::clamp<std::int64_t>(*v, 0, 255))));
              }
            }
            // -1 (infinite) is understood since 1.19.4 (DV 3337); older
            // targets treat 0 as the closest "no expiry" default.
            std::int32_t duration = (ctx.tgt_dv() >= 3337) ? -1 : 0;
            if (const NbtTag* d = FindChild(fx, "duration")) {
              if (const auto v = IntOf(*d)) {
                duration = static_cast<std::int32_t>(*v);
              }
            }
            o.children.push_back(MakeIntTag("Duration", duration));
            if (const NbtTag* a = FindChild(fx, "ambient")) {
              if (BoolOf(*a).value_or(false)) {
                o.children.push_back(MakeByteTag("Ambient", 1));
              }
            }
            if (const NbtTag* sp = FindChild(fx, "show_particles")) {
              if (!BoolOf(*sp).value_or(true)) {
                o.children.push_back(MakeByteTag("ShowParticles", 0));
              }
            }
            if (const NbtTag* si = FindChild(fx, "show_icon")) {
              if (!BoolOf(*si).value_or(true)) {
                o.children.push_back(MakeByteTag("ShowIcon", 0));
              }
            }
            any = true;
          }
        }
      }
      if (!any) return false;
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kProfile: {
      if (!WriteSkullOwner(
              ctx, comp, item, "SkullOwner", scratch, rule.rule_id, path)) {
        return false;
      }
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kStewEffects: {
      const NbtList* list =
          comp.type == TagType::kList ? AsList(comp) : nullptr;
      if (list == nullptr) return false;
      NbtList& out = EnsureList(item, "Effects", TagType::kCompound);
      bool any = false;
      for (const auto& e : list->children) {
        const auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
        if (ep == nullptr || *ep == nullptr) continue;
        const NbtCompound& fx = **ep;
        std::string_view fx_id;
        if (const NbtTag* idt = FindChild(fx, "id")) fx_id = AsString(*idt);
        const PotionEffectRule* r =
            fx_id.empty() ? nullptr : FindPotionEffectId(fx_id);
        if (r == nullptr) {
          ++ctx.stats().counters.effects_dropped;
          ctx.Report(Diag::kEffectNoTarget, rule.rule_id, path);
          continue;
        }
        NbtCompound& o = AppendListElementCompound(out);
        o.children.push_back(
            MakeByteTag("EffectId", static_cast<std::int8_t>(r->numeric)));
        std::int32_t duration = 160;  // default effect duration
        if (const NbtTag* d = FindChild(fx, "duration")) {
          if (const auto v = IntOf(*d)) {
            duration = static_cast<std::int32_t>(*v);
          }
        }
        o.children.push_back(MakeIntTag("EffectDuration", duration));
        any = true;
      }
      if (!any) return false;
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kBundleContents: {
      NbtList* entries = AsListMutable(comp);
      if (entries == nullptr || entries->element_type != TagType::kCompound) {
        return false;
      }
      NbtList& items = EnsureList(item, "Items", TagType::kCompound);
      std::int64_t index = 0;
      bool any = false;
      for (auto& e : entries->children) {
        auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
        if (ep == nullptr || *ep == nullptr) continue;
        NbtCompound& src = **ep;
        auto shell = std::make_unique<NbtCompound>();
        for (auto& f : src.children) {
          // `slot` (the bundle weight) has no target-era equivalent; the
          // target bundle is positional, so the index below is used.
          if (f.name == "slot") continue;
          if (f.name == "id" || f.name == "count" || f.name == "components") {
            shell->children.push_back(std::move(f));
          }
        }
        if (FindChild(*shell, "id") == nullptr) continue;  // not an item
        if (FindChild(*shell, "count") == nullptr) {
          shell->children.push_back(MakeIntTag("count", 1));
        }
        DowngradeItemCompound(ctx, *shell, scratch, path);
        if (index > 127) {
          ++ctx.stats().counters.item_count_clamps;
          ctx.Report(Diag::kItemCountClamped, rule.rule_id, path);
          break;
        }
        RemoveChild(*shell, "Slot");
        shell->children.push_back(
            MakeByteTag("Slot", static_cast<std::int8_t>(index)));
        items.children.push_back(NbtPayload{std::move(shell)});
        ++index;
        any = true;
      }
      if (!any) return false;
      ctx.Report(Diag::kContainerToItems, rule.rule_id, path);
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kWritableBook: {
      NbtList* pages = AsListMutable(comp);
      if (pages == nullptr) return false;
      NbtList& out = EnsureList(item, "pages", TagType::kString);
      bool any = false;
      for (const auto& e : pages->children) {
        if (const auto* sv = std::get_if<std::string_view>(&e)) {
          out.children.push_back(NbtPayload{*sv});
          any = true;
          continue;
        }
        const auto* pc = std::get_if<std::unique_ptr<NbtCompound>>(&e);
        if (pc == nullptr || *pc == nullptr) continue;
        const NbtTag* raw = FindChild(**pc, "raw");
        if (raw == nullptr) continue;
        const std::string_view s = AsString(*raw);
        if (s.empty()) continue;
        out.children.push_back(NbtPayload{s});
        if (FindChild(**pc, "filtered") != nullptr) {
          ctx.Report(Diag::kBookFilteredDropped, rule.rule_id, path);
        }
        any = true;
      }
      if (!any) return false;
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kWrittenBook: {
      if (body == nullptr) return false;
      bool any = false;
      if (const NbtTag* tt = FindChild(*body, "title")) {
        std::string_view title;
        if (const std::string_view s = AsString(*tt); !s.empty()) {
          title = s;
        } else if (const NbtCompound* tc = AsCompound(*tt)) {
          if (const NbtTag* raw = FindChild(*tc, "raw")) {
            title = AsString(*raw);
          }
          if (FindChild(*tc, "filtered") != nullptr) {
            ctx.Report(Diag::kBookFilteredDropped, rule.rule_id, path);
          }
        }
        if (!title.empty()) {
          if (ctx.src_dv() >= kDvSnbtText) {
            scratch.push_back(WrapPlainTextAsJson(title));
            item.children.push_back(StringTag("title", scratch.back()));
          } else {
            item.children.push_back(StringTag("title", title));
          }
          any = true;
        }
      }
      if (const NbtTag* at = FindChild(*body, "author")) {
        const std::string_view a = AsString(*at);
        if (!a.empty()) {
          item.children.push_back(StringTag("author", a));
          any = true;
        }
      }
      if (const NbtTag* gt = FindChild(*body, "generation")) {
        if (const auto v = IntOf(*gt)) {
          item.children.push_back(
              MakeIntTag("generation", static_cast<std::int32_t>(*v)));
          any = true;
        }
      }
      if (const NbtTag* rt = FindChild(*body, "resolved")) {
        if (BoolOf(*rt).value_or(false)) {
          item.children.push_back(MakeByteTag("resolved", 1));
          any = true;
        }
      }
      if (const NbtTag* pt = FindChild(*body, "pages")) {
        if (const NbtList* pages = AsList(*pt)) {
          NbtList& out = EnsureList(item, "pages", TagType::kString);
          for (const auto& e : pages->children) {
            if (const auto* sv = std::get_if<std::string_view>(&e)) {
              if (ctx.src_dv() >= kDvSnbtText) {
                scratch.push_back(WrapPlainTextAsJson(*sv));
                out.children.push_back(
                    NbtPayload{std::string_view(scratch.back())});
              } else {
                out.children.push_back(NbtPayload{*sv});
              }
              any = true;
              continue;
            }
            const auto* pc = std::get_if<std::unique_ptr<NbtCompound>>(&e);
            if (pc == nullptr || *pc == nullptr) continue;
            const NbtTag* raw = FindChild(**pc, "raw");
            if (raw == nullptr) continue;
            const std::string_view s = AsString(*raw);
            if (s.empty()) continue;
            if (ctx.src_dv() >= kDvSnbtText) {
              if (s.front() != '{') {
                scratch.push_back(WrapPlainTextAsJson(s));
                out.children.push_back(
                    NbtPayload{std::string_view(scratch.back())});
              } else {
                out.children.push_back(NbtPayload{s});
              }
            } else {
              out.children.push_back(NbtPayload{s});
            }
            if (FindChild(**pc, "filtered") != nullptr) {
              ctx.Report(Diag::kBookFilteredDropped, rule.rule_id, path);
            }
            any = true;
          }
        }
      }
      if (!any) return false;
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kLodestoneTracker: {
      if (body == nullptr) return false;
      bool any = false;
      if (const NbtTag* target = FindChild(*body, "target")) {
        if (const NbtCompound* tc = AsCompound(*target)) {
          if (const NbtTag* pos = FindChild(*tc, "pos")) {
            if (pos->type == TagType::kIntArray) {
              if (const auto* raw =
                      std::get_if<std::span<const std::byte>>(&pos->payload)) {
                const std::vector<std::int32_t> ints = ReadIntArray(*raw);
                if (ints.size() == 3) {
                  NbtCompound pos_compound;
                  pos_compound.children.push_back(MakeIntTag("X", ints[0]));
                  pos_compound.children.push_back(MakeIntTag("Y", ints[1]));
                  pos_compound.children.push_back(MakeIntTag("Z", ints[2]));
                  item.children.push_back(MakeCompoundTag(
                      "LodestonePos",
                      std::make_unique<NbtCompound>(std::move(pos_compound))));
                  any = true;
                }
              }
            }
          }
          if (const NbtTag* dim = FindChild(*tc, "dimension")) {
            const std::string_view d = AsString(*dim);
            if (!d.empty()) {
              item.children.push_back(StringTag("LodestoneDimension", d));
              any = true;
            }
          }
        }
      }
      if (const NbtTag* tr = FindChild(*body, "tracked")) {
        if (!BoolOf(*tr).value_or(true)) {
          item.children.push_back(MakeByteTag("LodestoneTracked", 0));
          any = true;
        }
      }
      if (!any) return false;
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kMapId: {
      if (comp.type != TagType::kInt) return false;
      const auto* v = std::get_if<std::int32_t>(&comp.payload);
      if (v == nullptr) return false;
      item.children.push_back(MakeIntTag("map", *v));
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kChargedProjectiles: {
      NbtList* entries = AsListMutable(comp);
      if (entries == nullptr || entries->element_type != TagType::kCompound) {
        return false;
      }
      NbtList& out = EnsureList(item, "ChargedProjectiles", TagType::kCompound);
      bool any = false;
      for (auto& e : entries->children) {
        auto* ep = std::get_if<std::unique_ptr<NbtCompound>>(&e);
        if (ep == nullptr || *ep == nullptr) continue;
        if (FindChild(**ep, "count") == nullptr) {
          (*ep)->children.push_back(MakeIntTag("count", 1));
        }
        DowngradeItemCompound(ctx, **ep, scratch, path);
        out.children.push_back(std::move(e));
        any = true;
      }
      if (!any) return false;
      item.children.push_back(MakeByteTag("Charged", 1));
      ++ctx.stats().counters.components_mapped;
      return true;
    }

    case ComponentKind::kText:
    case ComponentKind::kEnchantments:
    case ComponentKind::kStoredEnchantments:
    case ComponentKind::kDamage:
    case ComponentKind::kMaxDamage:
    case ComponentKind::kCustomModelData:
    case ComponentKind::kUnbreakable:
    case ComponentKind::kCharged:
    case ComponentKind::kDyedColor:
    case ComponentKind::kMapColor:
    case ComponentKind::kCustomData:
    case ComponentKind::kContainer:
    case ComponentKind::kTooltipDisplay:
    case ComponentKind::kDroppedKnown:
    case ComponentKind::kRenameToField:
    case ComponentKind::kEntityData:
    case ComponentKind::kBlockEntityData:
    case ComponentKind::kBucketEntityData:
    case ComponentKind::kBees:
    case ComponentKind::kContainerLoot:
    case ComponentKind::kPotDecorations:
    case ComponentKind::kBannerPatterns:
    case ComponentKind::kAxolotlVariant:
    case ComponentKind::kTropicalFishData:
      break;
  }
  return false;
}

}  // namespace fschema::remap::internal