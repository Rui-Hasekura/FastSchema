// FastSchema remap: BE id heuristic guess table (EnsureTileEntityId).
//
// Row shape: {primary, {additional[0], additional[1]}, additional_count, id}
//   * primary          : field that must exist in the BE compound
//   * additional       : up to 2 more fields that must also exist
//   * additional_count : how many `additional` entries are used (0/1/2)
//   * id               : guessed BE id when the row matches
//
// Order-sensitive: more specific rules must come before generic ones.
// The `{"Items", ..., "chest"}` row is the fallback for any BE carrying
// an inventory but no id.

{"Bees",                     {},                                  0, "minecraft:beehive"},
{"bees",                     {},                                  0, "minecraft:beehive"},
{"TransferCooldown",         {"Items", {}},                       1, "minecraft:hopper"},
{"SkullOwner",               {},                                  0, "minecraft:skull"},
{"Patterns",                 {},                                  0, "minecraft:banner"},
{"patterns",                 {},                                  0, "minecraft:banner"},
{"Sherds",                   {},                                  0, "minecraft:decorated_pot"},
{"sherds",                   {},                                  0, "minecraft:decorated_pot"},
{"last_interacted_slot",     {"Items", {}},                       1, "minecraft:chiseled_bookshelf"},
{"CookTime",                 {"Items", {}},                       1, "minecraft:furnace"},
{"RecordItem",               {},                                  0, "minecraft:jukebox"},
{"Book",                     {},                                  0, "minecraft:lectern"},
{"book",                     {},                                  0, "minecraft:lectern"},
{"front_text",               {},                                  0, "minecraft:sign"},
{"back_text",                {},                                  0, "minecraft:sign"},
{"BrewTime",                 {},                                  0, "minecraft:brewing_stand"},
{"Fuel",                     {},                                  0, "minecraft:brewing_stand"},
{"LootTable",                {"LootTableSeed", {}},               1, "minecraft:suspicious_sand"},
{"hit_direction",            {"item", {}},                        1, "minecraft:suspicious_sand"},
{"SpawnData",                {},                                  0, "minecraft:spawner"},
{"SpawnPotentials",          {},                                  0, "minecraft:spawner"},
{"normal_config",            {},                                  0, "minecraft:trial_spawner"},
{"shared_data",              {},                                  0, "minecraft:vault"},
{"pool",                     {"final_state", "placement_priority"}, 2, "minecraft:jigsaw"},
{"author",                   {"metadata", "showboundingbox"},     2, "minecraft:structure_block"},
{"ExactTeleport",            {"Age", {}},                         1, "minecraft:end_gateway"},
{"Items",                    {},                                  0, "minecraft:chest"},
{"last_vibration_frequency", {"listener", {}},                    1, "minecraft:sculk_sensor"},
{"warning_level",            {"listener", {}},                    1, "minecraft:sculk_shrieker"},
{"OutputSignal",             {},                                  0, "minecraft:comparator"},
{"facing",                   {},                                  0, "minecraft:piston"},
{"extending",                {},                                  0, "minecraft:piston"},
{"x",                        {"y", "z"},                          2, "minecraft:piston"},
{"Primary",                  {},                                  0, "minecraft:beacon"},
{"Secondary",                {},                                  0, "minecraft:beacon"},
{"primary_effect",           {},                                  0, "minecraft:beacon"},
{"secondary_effect",         {},                                  0, "minecraft:beacon"},