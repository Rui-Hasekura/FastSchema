// FastSchema remap: component -> tag mapping table.
//
// Row shape: {component, kind, anchor_dv, rule_id, [target, rename_target]}
//   * component     : source-era component id (must stay sorted)
//   * kind          : transform dispatch (see rule.h ComponentKind)
//   * anchor_dv     : earliest source DataVersion the component can appear in
//   * rule_id       : stable id surfaced in diagnostics
//   * target        : (kRenameToField only) destination field name
//   * rename_target : (kRenameToField only) destination compound
//
// Intentionally still unmapped (surface as kComponentDropped with the
// component name in the path - the discovery signal for the next slice):
//   * intangible_projectile / sign_text_* / waxed / dye: structural,
//     deferred.
//
// Transform notes (per kind):
//   * kText            custom_name/lore: value is a JSON string or a
//                      structured component (converted by text.cc).
//                      Target: tag.display.Name / tag.display.Lore.
//   * kEnchantments    {levels:{id:lvl}} or flat {id:lvl} ->
//                      tag.Enchantments [{id,lvl:short}].  Any
//                      show_in_tooltip=false folds into HideFlags bit 1.
//   * kStoredEnchantments  same shape -> tag.StoredEnchantments;
//                      show_in_tooltip folds into HideFlags bit 1.
//   * kDamage          int -> tag.Damage.
//   * kMaxDamage       int -> tag.MaxDamage.
//   * kCustomModelData int -> tag.CustomModelData; compound
//                      {floats:[...]} -> int from floats[0].
//   * kUnbreakable     byte or compound -> tag.Unbreakable:1b; false
//                      show_in_tooltip folds into HideFlags bit 4.
//   * kCharged         present/compound -> tag.Charged:1b.
//   * kChargedProjectiles  [{item}] -> tag.ChargedProjectiles + tag.Charged:1b.
//   * kDyedColor       {rgb:int} -> tag.display.color; show_in_tooltip=false
//                      folds into HideFlags bit 64.
//   * kMapColor        int -> tag.display.MapColor.
//   * kMapId           int -> tag.map (filled map id).
//   * kPotionContents  {potion} -> tag.Potion; custom_color:int ->
//                      tag.customPotionColor; custom_effects:[{id,...}] ->
//                      tag.custom_potion_effects [{Id:byte,...}] via the
//                      numeric table.
//   * kCustomData      compound merged into tag root (base layer, real
//                      components take precedence; collisions diagnosed).
//   * kContainer       [{slot:int, item:{...}}] -> tag.Items with Slot bytes.
//   * kTooltipDisplay  {hide_tooltip / hidden_components} folded
//                      into HideFlags bits.
//   * kTrim            {pattern, material} (string or {id} compound) ->
//                      tag.Trim {pattern, material}; show_in_tooltip=false
//                      folds into HideFlags bit 32 (additional).
//   * kFireworks       {flight_duration:int, explosions:[...]} ->
//                      tag.Fireworks {Flight:b, Explosions:[...]}.
//   * kFireworkExplosion  explosion body -> tag.Explosion {Type:b (shape
//                      enum), Colors:[I], FadeColors:[I], Trail, Flicker}.
//   * kAttributeModifiers  bare list or {modifiers, show_in_tooltip} ->
//                      tag.AttributeModifiers [{AttributeName, Name,
//                      Amount:double, Operation:int, UUID:[I;4], Slot}].
//   * kCanPlaceOn      {predicates:[{blocks, state, nbt}], show_in_tooltip}
//                      or bare predicate list -> tag.CanPlaceOn [id strings];
//                      state/nbt restrictions are not expressible (item
//                      becomes more permissive).
//   * kCanBreak        same -> tag.CanDestroy.
//   * kProfile         {name, id, properties} -> tag.SkullOwner {Name,
//                      Id:[I;4], Properties}.
//   * kStewEffects     [{id, duration}] -> tag.Effects [{EffectId:b,
//                      EffectDuration:i}] via the numeric table.
//   * kBundleContents  [{slot:f, item}] -> tag.Items [{Slot:b, ...}] with
//                      recursive item downgrade; source float `slot` is
//                      dropped and the output Slot is the list index as a byte.
//   * kWritableBook    [{raw, filtered?}] -> tag.pages [raw strings];
//                      filtered text dropped.
//   * kWrittenBook     {title, author, generation, resolved, pages} ->
//                      tag {title, author, generation:i, resolved, pages}.
//   * kLodestoneTracker  {target:{pos, dimension}, tracked} ->
//                      tag.LodestonePos / tag.LodestoneDimension /
//                      tag.LodestoneTracked:0b.
//   * kDroppedKnown    known component with no target-era equivalent (food,
//                      tool, consumable, ...): counted + diagnosed with its
//                      own rule_id; honors UnmappedComponentPolicy.
//   * kRenameToField   pure rename to a target field:
//                      payload moved verbatim (no value transform).
//                      kBlockEntityTag additionally gets an `id` synthesized
//                      from the item id when the component omits one.
//   * kBannerPatterns  [{pattern, color, ...}] -> tag.BlockEntityTag.Patterns
//                      [{Color:int, Pattern:string}]; be_tag.id is set to
//                      "minecraft:banner".
//   * kMapDecorations  {key: {x, z, rotation, type}} ->
//                      tag.Decorations [{id:key, x:d, z:d, rot:d, type:b}].

{"minecraft:attribute_modifiers",  ComponentKind::kAttributeModifiers,  3837, 0x0210},
{"minecraft:axolotl/variant",      ComponentKind::kAxolotlVariant,     3837, 0x0250},
{"minecraft:banner_patterns",      ComponentKind::kBannerPatterns,      3837, 0x024E},
{"minecraft:bees",                 ComponentKind::kBees,                3837, 0x0248},
{"minecraft:block_entity_data",    ComponentKind::kBlockEntityData,      3837, 0x0249},
{"minecraft:block_state",          ComponentKind::kRenameToField,        3837, 0x0242, "BlockStateTag",    RenameTarget::kItemRoot},
{"minecraft:bucket_entity_data",   ComponentKind::kBucketEntityData,     3837, 0x024A},
{"minecraft:bundle_contents",      ComponentKind::kBundleContents,      3837, 0x0211},
{"minecraft:can_break",            ComponentKind::kCanBreak,            3837, 0x0212},
{"minecraft:can_place_on",         ComponentKind::kCanPlaceOn,         3837, 0x0213},
{"minecraft:charged",              ComponentKind::kCharged,             3837, 0x0201},
{"minecraft:charged_projectiles",  ComponentKind::kChargedProjectiles,  3837, 0x021D},
{"minecraft:consumable",           ComponentKind::kDroppedKnown,        4058, 0x0230},
{"minecraft:container",            ComponentKind::kContainer,           3837, 0x0202},
{"minecraft:container_loot",       ComponentKind::kContainerLoot,       3837, 0x024B},
{"minecraft:custom_data",          ComponentKind::kCustomData,          3837, 0x0203},
{"minecraft:custom_model_data",    ComponentKind::kCustomModelData,     3837, 0x0204},
{"minecraft:custom_name",          ComponentKind::kText,                3837, 0x0205},
{"minecraft:damage",               ComponentKind::kDamage,              3837, 0x0206},
{"minecraft:damage_resistant",     ComponentKind::kDroppedKnown,        4058, 0x0231},
{"minecraft:debug_stick_state",    ComponentKind::kRenameToField,        3837, 0x0244, "DebugProperty",    RenameTarget::kItemRoot},
{"minecraft:dyed_color",           ComponentKind::kDyedColor,           3837, 0x0207},
{"minecraft:enchantable",          ComponentKind::kDroppedKnown,        4058, 0x0232},
{"minecraft:enchantment_glint_override", ComponentKind::kDroppedKnown,  3837, 0x0233},
{"minecraft:enchantments",         ComponentKind::kEnchantments,        3837, 0x0208},
{"minecraft:entity_data",          ComponentKind::kEntityData,           3837, 0x024C},
{"minecraft:equippable",           ComponentKind::kDroppedKnown,        3837, 0x0234},
{"minecraft:firework_explosion",   ComponentKind::kFireworkExplosion,   3837, 0x0214},
{"minecraft:fireworks",            ComponentKind::kFireworks,           3837, 0x0215},
{"minecraft:food",                 ComponentKind::kDroppedKnown,        3837, 0x0235},
{"minecraft:glider",               ComponentKind::kDroppedKnown,        4058, 0x0236},
{"minecraft:instrument",           ComponentKind::kRenameToField,       3837, 0x0240, "instrument",       RenameTarget::kItemRoot},
{"minecraft:item_name",            ComponentKind::kText,                 3837, 0x024D},
{"minecraft:jukebox_playable",     ComponentKind::kDroppedKnown,        3837, 0x0237},
{"minecraft:lock",                 ComponentKind::kRenameToField,        3837, 0x0247, "Lock",             RenameTarget::kBlockEntityTag},
{"minecraft:lodestone_tracker",    ComponentKind::kLodestoneTracker,    3837, 0x0216},
{"minecraft:lore",                 ComponentKind::kText,                3837, 0x0209},
{"minecraft:map_color",            ComponentKind::kMapColor,            3837, 0x020A},
{"minecraft:map_decorations",      ComponentKind::kMapDecorations,      3837, 0x024F},
{"minecraft:map_id",               ComponentKind::kMapId,               3837, 0x0217},
{"minecraft:max_damage",           ComponentKind::kMaxDamage,           3837, 0x020B},
{"minecraft:max_stack_size",       ComponentKind::kDroppedKnown,        3837, 0x0238},
{"minecraft:note_block_sound",     ComponentKind::kRenameToField,        3837, 0x0245, "note_block_sound", RenameTarget::kItemRoot},
{"minecraft:pot_decorations",      ComponentKind::kPotDecorations,      3837, 0x0241},
{"minecraft:potion_contents",      ComponentKind::kPotionContents,      3837, 0x020C},
{"minecraft:potion_duration_scale", ComponentKind::kDroppedKnown,       4325, 0x023F},
{"minecraft:profile",              ComponentKind::kProfile,             3837, 0x0218},
{"minecraft:rarity",               ComponentKind::kDroppedKnown,        3837, 0x0239},
{"minecraft:recipes",              ComponentKind::kRenameToField,        3837, 0x0243, "Recipes",          RenameTarget::kItemRoot},
{"minecraft:repair_cost",          ComponentKind::kRenameToField,        3837, 0x0246, "RepairCost",       RenameTarget::kItemRoot},
{"minecraft:repairable",           ComponentKind::kDroppedKnown,        4058, 0x023A},
{"minecraft:stored_enchantments",  ComponentKind::kStoredEnchantments,  3837, 0x021E},
{"minecraft:suspicious_stew_effects", ComponentKind::kStewEffects,      3837, 0x0219},
{"minecraft:tool",                 ComponentKind::kDroppedKnown,        3824, 0x023B},
{"minecraft:tooltip_display",      ComponentKind::kTooltipDisplay,      4325, 0x020D},
{"minecraft:trim",                 ComponentKind::kTrim,                3837, 0x021A},
{"minecraft:tropical_fish/base_color",    ComponentKind::kTropicalFishData, 3837, 0x0251},
{"minecraft:tropical_fish/pattern",       ComponentKind::kTropicalFishData, 3837, 0x0252},
{"minecraft:tropical_fish/pattern_color", ComponentKind::kTropicalFishData, 3837, 0x0253},
{"minecraft:unbreakable",          ComponentKind::kUnbreakable,         3837, 0x020E},
{"minecraft:use_cooldown",         ComponentKind::kDroppedKnown,        4058, 0x023C},
{"minecraft:use_remainder",        ComponentKind::kDroppedKnown,        3837, 0x023D},
{"minecraft:weapon",               ComponentKind::kDroppedKnown,        4325, 0x023E},
{"minecraft:writable_book_content", ComponentKind::kWritableBook,       3837, 0x021B},
{"minecraft:written_book_content", ComponentKind::kWrittenBook,         3837, 0x021C},