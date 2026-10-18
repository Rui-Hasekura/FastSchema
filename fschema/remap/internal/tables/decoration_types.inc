// FastSchema remap: map decoration type id -> numeric decoration type.
//
// Row shape: {type_id, numeric}
//   * type_id : namespaced source-era decoration id.
//   * numeric : 0..33 in the classic MapDecoration order.
//
// Unknown ids fall back to 0 (player) at the call site with a
// kBeFieldNoTarget diagnostic.

{"minecraft:player",            0},
{"minecraft:frame",             1},
{"minecraft:red_marker",        2},
{"minecraft:blue_marker",       3},
{"minecraft:target_x",          4},
{"minecraft:target_point",      5},
{"minecraft:player_off_map",    6},
{"minecraft:player_off_limits", 7},
{"minecraft:mansion",           8},
{"minecraft:monument",          9},
{"minecraft:banner_white",      10},
{"minecraft:banner_orange",     11},
{"minecraft:banner_magenta",    12},
{"minecraft:banner_light_blue", 13},
{"minecraft:banner_yellow",     14},
{"minecraft:banner_lime",       15},
{"minecraft:banner_pink",       16},
{"minecraft:banner_gray",       17},
{"minecraft:banner_light_gray", 18},
{"minecraft:banner_cyan",       19},
{"minecraft:banner_purple",     20},
{"minecraft:banner_blue",       21},
{"minecraft:banner_brown",      22},
{"minecraft:banner_green",      23},
{"minecraft:banner_red",        24},
{"minecraft:banner_black",      25},
{"minecraft:red_x",             26},
{"minecraft:village_desert",    27},
{"minecraft:village_plains",    28},
{"minecraft:village_savanna",   29},
{"minecraft:village_snowy",     30},
{"minecraft:village_taiga",     31},
{"minecraft:jungle_temple",     32},
{"minecraft:swamp_hut",         33},