// FastSchema remap: banner pattern id -> short code.
//
// Row shape: {pattern_id, short_code}
//   * pattern_id : namespaced source-era pattern id ("minecraft:base", ...)
//   * short_code : target-era two/three-letter code used by the BE `Patterns`
//                  list (e.g. "b", "bl").
//
// Unknown ids fall back to "b" (base) at the call site with a
// kBeFieldNoTarget diagnostic.

{"minecraft:base",                     "b"},
{"minecraft:square_bottom_left",       "bl"},
{"minecraft:square_bottom_right",      "br"},
{"minecraft:square_top_left",          "tl"},
{"minecraft:square_top_right",         "tr"},
{"minecraft:stripe_bottom",            "bs"},
{"minecraft:stripe_top",               "ts"},
{"minecraft:stripe_left",              "ls"},
{"minecraft:stripe_right",             "rs"},
{"minecraft:stripe_center",            "cs"},
{"minecraft:stripe_middle",            "ms"},
{"minecraft:stripe_downright",         "drs"},
{"minecraft:stripe_downleft",          "dls"},
{"minecraft:small_stripes",            "ss"},
{"minecraft:cross",                    "cr"},
{"minecraft:straight_cross",           "sc"},
{"minecraft:triangle_bottom",          "bt"},
{"minecraft:triangle_top",             "tt"},
{"minecraft:triangles_bottom",         "bts"},
{"minecraft:triangles_top",            "tts"},
{"minecraft:diagonal_left",            "ld"},
{"minecraft:diagonal_up_right",        "rd"},
{"minecraft:diagonal_up_left",         "lud"},
{"minecraft:diagonal_right",           "rud"},
{"minecraft:circle",                   "mc"},
{"minecraft:rhombus",                  "mr"},
{"minecraft:half_vertical",            "vh"},
{"minecraft:half_horizontal",          "hh"},
{"minecraft:half_vertical_right",      "vhr"},
{"minecraft:half_horizontal_bottom",   "hhb"},
{"minecraft:border",                   "bo"},
{"minecraft:curly_border",             "cbo"},
{"minecraft:gradient",                 "gra"},
{"minecraft:gradient_up",              "gru"},
{"minecraft:bricks",                   "bri"},
{"minecraft:globe",                    "glb"},
{"minecraft:creeper",                  "cre"},
{"minecraft:skull",                    "sku"},
{"minecraft:flower",                   "flo"},
{"minecraft:mojang",                   "moj"},
{"minecraft:piglin",                   "pig"},