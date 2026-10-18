// FastSchema remap: beacon effect id -> numeric MobEffects id.
//
// Row shape: {effect_id, numeric}
//   * effect_id : namespaced source-era effect id.
//   * numeric   : target-era numeric MobEffects id used by the BE
//                 `Primary` / `Secondary` fields.
//
// Only the six effects the target beacon accepts are listed; anything else
// falls back to 1 (speed) at the call site with a kBeFieldNoTarget
// diagnostic.

{"minecraft:haste",        3},
{"minecraft:jump_boost",   8},
{"minecraft:regeneration", 10},
{"minecraft:resistance",   11},
{"minecraft:speed",        1},
{"minecraft:strength",     5},