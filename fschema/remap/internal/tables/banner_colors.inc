// FastSchema remap: banner color id -> numeric color.
//
// Row shape: {color_id, numeric}
//   * color_id : bare (un-namespaced) color name.
//   * numeric  : 0..15 in the classic white..black order.
//
// Unknown colors fall back to 0 at the call site (kBeFieldNoTarget diagnostic).

{"white",      0},
{"orange",     1},
{"magenta",    2},
{"light_blue", 3},
{"yellow",     4},
{"lime",       5},
{"pink",       6},
{"gray",       7},
{"light_gray", 8},
{"cyan",       9},
{"purple",    10},
{"blue",      11},
{"brown",     12},
{"green",     13},
{"red",       14},
{"black",     15},