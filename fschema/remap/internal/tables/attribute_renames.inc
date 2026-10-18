// FastSchema remap: attribute id rollback table.
//
// Row shape: {newer_id, older_id, anchor_dv, rule_id}
//   * newer_id  : source-era attribute id (must stay sorted)
//   * older_id  : target-era attribute id; EMPTY means the attribute has
//                 no target-era equivalent (introduced later) -> the
//                 modifier is dropped with kAttributeNoTarget diagnostics.
//   * anchor_dv : first dv where `newer_id` exists.
//
// Unknown ids (e.g. datapack attributes) are kept verbatim and diagnosed;
// the target version ignores unknown AttributeName values gracefully.

{"minecraft:air_drag_modifier",           {},                                    4325, 0x0425},
{"minecraft:armor",                       "minecraft:generic.armor",             4058, 0x0401},
{"minecraft:armor_toughness",             "minecraft:generic.armor_toughness",   4058, 0x0402},
{"minecraft:attack_damage",               "minecraft:generic.attack_damage",     4058, 0x0403},
{"minecraft:attack_knockback",            "minecraft:generic.attack_knockback",  4058, 0x0404},
{"minecraft:attack_speed",                "minecraft:generic.attack_speed",      4058, 0x0405},
{"minecraft:below_name_distance",         {},                                    4325, 0x0426},
{"minecraft:block_break_speed",           {},                                    4058, 0x0415},
{"minecraft:block_interaction_range",     {},                                    3837, 0x0411},
{"minecraft:bounciness",                  {},                                    4325, 0x0422},
{"minecraft:burning_time",                {},                                    4325, 0x0421},
{"minecraft:camera_distance",             {},                                    4325, 0x0427},
{"minecraft:entity_interaction_range",    {},                                    3837, 0x0412},
{"minecraft:explosion_knockback_resistance", {},                                4058, 0x0419},
{"minecraft:fall_damage_multiplier",      {},                                    4325, 0x0423},
{"minecraft:flying_speed",                "minecraft:generic.flying_speed",      4058, 0x0406},
{"minecraft:follow_range",                "minecraft:generic.follow_range",      4058, 0x0407},
{"minecraft:friction_modifier",           {},                                    4325, 0x0424},
{"minecraft:generic.jump_strength",       "minecraft:horse.jump_strength",       3815, 0x040F},
{"minecraft:generic.scale",               "minecraft:generic.scale",             3801, 0x0413},
{"minecraft:generic.spawn_reinforcements", "minecraft:zombie.spawn_reinforcements", 3815, 0x0410},
{"minecraft:gravity",                     {},                                    4058, 0x041A},
{"minecraft:jump_strength",               "minecraft:horse.jump_strength",       4058, 0x0408},
{"minecraft:knockback_resistance",        "minecraft:generic.knockback_resistance", 4058, 0x0409},
{"minecraft:luck",                        "minecraft:generic.luck",              4058, 0x040A},
{"minecraft:max_absorption",              {},                                    4058, 0x040B},
{"minecraft:max_health",                  "minecraft:generic.max_health",        4058, 0x040C},
{"minecraft:mining_efficiency",           {},                                    4058, 0x0416},
{"minecraft:movement_efficiency",         {},                                    4058, 0x041C},
{"minecraft:movement_speed",              "minecraft:generic.movement_speed",    4058, 0x040D},
{"minecraft:name_tag_distance",           {},                                    4325, 0x0428},
{"minecraft:oxygen_bonus",                {},                                    4058, 0x041D},
{"minecraft:safe_fall_distance",          {},                                    4058, 0x041B},
{"minecraft:scale",                       "minecraft:generic.scale",             4058, 0x0413},
{"minecraft:sneaking_speed",              {},                                    4058, 0x0417},
{"minecraft:spawn_reinforcements",        "minecraft:zombie.spawn_reinforcements", 4058, 0x040E},
{"minecraft:step_height",                 {},                                    3837, 0x0414},
{"minecraft:submerged_mining_speed",      {},                                    4058, 0x0418},
{"minecraft:sweeping_damage_ratio",       {},                                    4058, 0x041F},
{"minecraft:tempt_range",                 {},                                    4058, 0x0420},
{"minecraft:water_movement_efficiency",   {},                                    4058, 0x041E},
{"minecraft:waypoint_receive_range",      {},                                    4554, 0x0429},
{"minecraft:waypoint_transmit_range",     {},                                    4554, 0x042A},