// FastSchema remap: status effect string id -> numeric id.
//
// Row shape: {id, numeric}
//   * id      : source-era effect string id (namespaced; must stay sorted).
//               Bare (un-namespaced) ids are accepted at lookup time.
//   * numeric : target-era MobEffects registry id, written as the legacy
//               `Id` / `EffectId` byte.
//
// Effects introduced after the target version have no numeric form and
// are not listed here: they surface as kEffectNoTarget diagnostics and
// the individual effect is dropped.

{"minecraft:absorption",          22},
{"minecraft:bad_omen",            31},
{"minecraft:blindness",           15},
{"minecraft:conduit_power",       29},
{"minecraft:darkness",            33},
{"minecraft:dolphins_grace",      30},
{"minecraft:fire_resistance",     12},
{"minecraft:glowing",             24},
{"minecraft:haste",                3},
{"minecraft:health_boost",        21},
{"minecraft:hero_of_the_village", 32},
{"minecraft:hunger",              17},
{"minecraft:instant_damage",       7},
{"minecraft:instant_health",       6},
{"minecraft:invisibility",        14},
{"minecraft:jump_boost",           8},
{"minecraft:levitation",          25},
{"minecraft:luck",                26},
{"minecraft:mining_fatigue",       4},
{"minecraft:nausea",               9},
{"minecraft:night_vision",        16},
{"minecraft:poison",              19},
{"minecraft:regeneration",        10},
{"minecraft:resistance",          11},
{"minecraft:saturation",          23},
{"minecraft:slow_falling",        28},
{"minecraft:slowness",             2},
{"minecraft:speed",                1},
{"minecraft:strength",             5},
{"minecraft:unluck",              27},
{"minecraft:water_breathing",     13},
{"minecraft:weakness",            18},
{"minecraft:wither",              20},