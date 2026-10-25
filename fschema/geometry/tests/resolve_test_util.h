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

#ifndef FSCHEMA_GEOMETRY_TESTS_RESOLVE_TEST_UTIL_H_
#define FSCHEMA_GEOMETRY_TESTS_RESOLVE_TEST_UTIL_H_

#include <cstddef>
#include <span>
#include <string_view>

#include "fschema/geometry/block_props.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/ir/types.h"

namespace fschema::tests {

// Builds a synthetic vanilla-shaped resource pack:
//   blockstates/stone.json       variants "" -> block/cube_all
//   blockstates/dirt.json        variants "" -> block/cube_all (+ dirt tex)
//   blockstates/oak_stairs.json  variants "facing=north,..." ->
//                                 block/oak_stairs
//   blockstates/missing_model.json  references a nonexistent model
//   models/block/cube_all.json   parent cube, textures all -> block/stone
//   models/block/cube.json       one cube element, 6 faces, each "#all"
//   models/block/oak_stairs.json parent cube, oak_planks texture
// Texture-resolution fixtures:
//   tex_child -> tex_parent    child redefines the parent's "side" var
//   chain_leaf -> chain_base   "#skin" -> "#inner" reference chain
//   sprite_form               object-form texture {"sprite": ...}
//   bare_name                 face "texture": "all" without "#"
//   orphan_child              parent model missing from disk
//   tint_leaves/tint_grass    faces with and without "tintindex",
//                             driven through oak/birch/cherry/grass
//                             blockstates to exercise tint-slot choice
//   textures/block/*.png         placeholder bytes
struct ResolveFixture {
  TempDir root;
  ResolveFixture() {
    const auto base = root.Get();

    WriteFile(base / "assets" / "minecraft" / "blockstates" / "stone.json",
              R"({"variants": {"": {"model": "block/cube_all"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "dirt.json",
        R"({"variants": {"": {"model": "block/cube_all",)"
        R"( "textures": { "all": "block/dirt" }}}})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "oak_stairs.json",
        R"({"variants": {"facing=north,half=bottom,shape=straight":)"
        R"( {"model": "block/oak_stairs"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "missing_model.json",
        R"({"variants": {"": {"model": "block/does_not_exist"}}})");

    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "cube_all.json",
        R"({"parent": "block/cube", "textures": {"all": "block/stone"}})");
    WriteFile(base / "assets" / "minecraft" / "models" / "block" / "cube.json",
              R"({"textures": {"particle": "block/stone"},)"
              R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
              R"( "faces": {)"
              R"(   "north": {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "south": {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "east":  {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "west":  {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "up":    {"texture": "#all", "uv": [0,0,16,16]},)"
              R"(   "down":  {"texture": "#all", "uv": [0,0,16,16]})"
              R"( }}]})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "oak_stairs.json",
        R"({"parent": "block/cube", "textures": {"all": "block/oak_planks"}})");

    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "tex_child.json",
        R"({"variants": {"": {"model": "block/tex_child"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "tex_parent.json",
        R"({"textures": {"side": "block/parent_side"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "faces": {"south": {"texture": "#side"}}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "tex_child.json",
        R"({"parent": "block/tex_parent",)"
        R"( "textures": {"side": "block/child_side"}})");

    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "chain_leaf.json",
        R"({"variants": {"": {"model": "block/chain_leaf"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "chain_base.json",
        R"({"textures": {"skin": "#inner"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "faces": {"south": {"texture": "#skin"}}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "chain_leaf.json",
        R"({"parent": "block/chain_base",)"
        R"( "textures": {"inner": "block/leaf_inner"}})");

    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "orphan_child.json",
        R"({"variants": {"": {"model": "block/orphan_child"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "orphan_child.json",
        R"({"parent": "block/no_such_parent"})");

    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "sprite_form.json",
        R"({"variants": {"": {"model": "block/sprite_form"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "sprite_form.json",
        R"({"parent": "block/cube_all", "textures": {"all": )"
        R"({"force_translucent": true, "sprite": "block/glass"}}})");

    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "bare_name.json",
        R"({"variants": {"": {"model": "block/bare_name"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "bare_name.json",
        R"({"textures": {"all": "block/heavy_core"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "faces": {"south": {"texture": "all"}}}]})");

    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "oak_leaves.json",
        R"({"variants": {"": {"model": "block/tint_leaves"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "birch_leaves.json",
        R"({"variants": {"": {"model": "block/tint_leaves"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" /
            "cherry_leaves.json",
        R"({"variants": {"": {"model": "block/tint_leaves"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "tint_leaves.json",
        R"({"textures": {"all": "block/oak_leaves"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "faces": {)"
        R"( "north": {"texture": "#all"},)"
        R"( "south": {"texture": "#all", "tintindex": 0}}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "grass_block.json",
        R"({"variants": {"": {"model": "block/tint_grass"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "tint_grass.json",
        R"({"textures": {"bottom": "block/dirt",)"
        R"( "top": "block/grass_block_top"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "faces": {)"
        R"( "down": {"texture": "#bottom"},)"
        R"( "up": {"texture": "#top", "tintindex": 0}}}]})");

    WriteFile(
        base / "assets" / "minecraft" / "textures" / "block" / "stone.png",
        std::string(4 * 16 * 16, '\xAA'));
    WriteFile(base / "assets" / "minecraft" / "textures" / "block" / "dirt.png",
              std::string(4 * 16 * 16, '\xBB'));
    WriteFile(
        base / "assets" / "minecraft" / "textures" / "block" / "oak_planks.png",
        std::string(4 * 16 * 16, '\xCC'));

    // Multipart `when` forms: OR/AND groups, pipe-separated and array
    // values, and an `apply` array (weighted pick in vanilla).
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "or_gate.json",
        R"({"multipart": [)"
        R"({"apply": {"model": "block/gate_a"},)"
        R"( "when": {"OR": [{"powered": "true"}, {"facing": "north"}]}},)"
        R"({"apply": {"model": "block/gate_b"},)"
        R"( "when": {"powered": "false"}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "and_gate.json",
        R"({"multipart": [)"
        R"({"apply": {"model": "block/gate_a"},)"
        R"( "when": {"AND": [{"powered": "true"}, {"facing": "north"}]}},)"
        R"({"apply": {"model": "block/gate_b"},)"
        R"( "when": {"powered": "true"}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "pipe_gate.json",
        R"({"multipart": [)"
        R"({"apply": {"model": "block/gate_a"},)"
        R"( "when": {"state": "side|up"}},)"
        R"({"apply": {"model": "block/gate_b"}, "when": {"state": "none"}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "array_gate.json",
        R"({"multipart": [)"
        R"({"apply": {"model": "block/gate_a"},)"
        R"( "when": {"facing": ["north", "south"]}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "first_pick.json",
        R"({"multipart": [)"
        R"({"apply": [{"model": "block/gate_a"},)"
        R"( {"model": "block/gate_b"}]}]})");
    for (const char* gate : {"gate_a", "gate_b"}) {
      const std::string name(gate);
      WriteFile(base / "assets" / "minecraft" / "models" / "block" /
                    (name + ".json"),
                R"({"textures": {"side": "block/)" + name +
                    R"(_tex"},)"
                R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
                R"( "faces": {"south": {"texture": "#side"}}}]})");
    }

    // Transparency: name-classified (glass), fluid synthesis (water),
    // and model-declared render_type overrides.
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "glass.json",
        R"({"variants": {"": {"model": "block/glass_cube"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "glass_cube.json",
        R"({"parent": "block/cube", "textures": {"all": "block/glass_tex"}})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "water.json",
        R"({"variants": {"": {"model": "block/water"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "water.json",
        R"({"textures": {"particle": "block/water_still"}})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rt_block.json",
        R"({"variants": {"": {"model": "block/rt_translucent"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "rt_translucent.json",
        R"({"parent": "block/cube", "textures": {"all": "block/rt_tex"},)"
        R"( "render_type": "minecraft:translucent"})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rt_solid.json",
        R"({"variants": {"": {"model": "block/rt_solid_child"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "rt_solid_child.json",
        R"({"parent": "block/rt_translucent",)"
        R"( "render_type": "minecraft:solid"})");

    // Element rotation: a full cube rotated 45 degrees about the block
    // center on Y (arbitrary angles are legal since 1.21.6), a 30-degree
    // variant, a cross-style element with rescale, and a zero-angle
    // control that must stay unrotated with its cull face intact.
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rot_slab.json",
        R"({"variants": {"": {"model": "block/rot_slab"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "rot_slab.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "rotation": {"origin": [8,8,8], "axis": "y", "angle": 45},)"
        R"( "faces": {"north": {"texture": "#all"}}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rot_slab30.json",
        R"({"variants": {"": {"model": "block/rot_slab30"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" /
            "rot_slab30.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "rotation": {"origin": [8,8,8], "axis": "y", "angle": 30},)"
        R"( "faces": {"north": {"texture": "#all"}}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rot_zero.json",
        R"({"variants": {"": {"model": "block/rot_zero"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "rot_zero.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "rotation": {"origin": [8,8,8], "axis": "y", "angle": 0},)"
        R"( "faces": {"north": {"texture": "#all"}}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rot_cross.json",
        R"({"variants": {"": {"model": "block/rot_cross"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "rot_cross.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [0.8,0,8], "to": [15.2,16,8],)"
        R"( "rotation": {"origin": [8,8,8], "axis": "y", "angle": 45,)"
        R"( "rescale": true},)"
        R"( "faces": {"north": {"texture": "#all"},)"
        R"( "south": {"texture": "#all"}}}]})");

        // Variant turns serialized as floats ("y": 90.0): real packs do
    // this, simdjson types them as doubles, and the reader must fall
    // back from get_int64 to get_double. Fractional (90.5) and
    // non-quarter (45.0) floats stay ignored like their int forms.
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rot_cull_float.json",
        R"({"variants": {)"
        R"("facing=south": {"model": "block/rot_zero", "y": 90.0},)"
        R"("facing=up": {"model": "block/rot_zero", "x": 180.0},)"
        R"("facing=skew": {"model": "block/rot_zero", "y": 90.5},)"
        R"("facing=half": {"model": "block/rot_zero", "y": 45.0}}})");
    // rescale at non-45 angles: vanilla's per-axis factor
    // 1 / max(|cos|, |sin|) equals 1/cos up to 45 degrees, so the
    // 22.5/30-degree strips keep their original x extent (0.05..0.95)
    // while their corners swing out by 0.45 * tan(angle).
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rot_cross22.json",
        R"({"variants": {"": {"model": "block/rot_cross22"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "rot_cross22.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [0.8,0,8], "to": [15.2,16,8],)"
        R"( "rotation": {"origin": [8,8,8], "axis": "y", "angle": 22.5,)"
        R"( "rescale": true},)"
        R"( "faces": {"north": {"texture": "#all"},)"
        R"( "south": {"texture": "#all"}}}]})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rot_cross30.json",
        R"({"variants": {"": {"model": "block/rot_cross30"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "rot_cross30.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [0.8,0,8], "to": [15.2,16,8],)"
        R"( "rotation": {"origin": [8,8,8], "axis": "y", "angle": 30,)"
        R"( "rescale": true},)"
        R"( "faces": {"north": {"texture": "#all"},)"
        R"( "south": {"texture": "#all"}}}]})");

    // Variant-level quarter turns and an ignored non-quarter value.
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" / "rot_turn.json",
        R"({"variants": {)"
        R"("facing=south": {"model": "block/rot_slab", "y": 90},)"
        R"("facing=up": {"model": "block/rot_slab", "x": 180},)"
        R"("facing=skew": {"model": "block/rot_slab", "y": 45}}})");
    WriteFile(
        base / "assets" / "minecraft" / "blockstates" /
            "rot_multipart.json",
        R"({"multipart": [{"apply": {"model": "block/rot_slab",)"
        R"( "y": 180}, "when": {"powered": "true"}}]})");
    // Cull remap coverage: rot_zero keeps its axis cull (zero angle),
    // so the variant quarter turn is the only transform acting on it.
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "rot_cull.json",
              R"({"variants": {)"
              R"("facing=south": {"model": "block/rot_zero", "y": 90},)"
              R"("facing=up": {"model": "block/rot_zero", "x": 180},)"
              R"("facing=west": {"model": "block/rot_zero", "y": 270}}})");
    // Element rotation about the x and z axes (y is covered by
    // rot_slab above): same shape, different axis.
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "rot_slab_x.json",
              R"({"variants": {"": {"model": "block/rot_slab_x"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "rot_slab_x.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "rotation": {"origin": [8,8,8], "axis": "x", "angle": 45},)"
        R"( "faces": {"north": {"texture": "#all"}}}]})");
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "rot_slab_z.json",
              R"({"variants": {"": {"model": "block/rot_slab_z"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "rot_slab_z.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [0,0,0], "to": [16,16,16],)"
        R"( "rotation": {"origin": [8,8,8], "axis": "z", "angle": 45},)"
        R"( "faces": {"north": {"texture": "#all"}}}]})");

    // uvlock coverage: a full cube under x=180 / y=90, with and
    // without the lock. cube_all gives six faces with uv [0,0,16,16].
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "rot_uvlock.json",
              R"({"variants": {)"
              R"("facing=up": {"model": "block/cube_all", "x": 180,)"
              R"( "uvlock": true},)"
              R"("facing=south": {"model": "block/cube_all", "y": 90,)"
              R"( "uvlock": true},)"
              R"("facing=down": {"model": "block/cube_all", "x": 180}}})");
    WriteFile(base / "assets" / "minecraft" / "blockstates" /
                  "rot_uvlock_multipart.json",
              R"({"multipart": [{"apply": {"model": "block/cube_all",)"
              R"( "x": 180, "uvlock": true},)"
              R"( "when": {"powered": "true"}}]})");

    // Element faces without an explicit uv rect derive it from the
    // element bounds, vanilla FaceBakery.defaultFaceUV style
    // (north/east/down mirror their axes). Asymmetric bounds make
    // every corner value distinguishable.
    WriteFile(base / "assets" / "minecraft" / "blockstates" / "auto_uv.json",
              R"({"variants": {"": {"model": "block/auto_uv"}}})");
    WriteFile(
        base / "assets" / "minecraft" / "models" / "block" / "auto_uv.json",
        R"({"textures": {"all": "block/stone"},)"
        R"( "elements": [{"from": [4, 6, 8], "to": [12, 14, 15],)"
        R"( "faces": {"north": {"texture": "#all"},)"
        R"( "south": {"texture": "#all"}, "east": {"texture": "#all"},)"
        R"( "west": {"texture": "#all"}, "up": {"texture": "#all"},)"
        R"( "down": {"texture": "#all"}}}]})");
  }
};

geometry::BlockProps EmptyProps() {
  ir::BlockState s{.name = "minecraft:test",
               .prop_encoding = ir::PropertyEncoding::kNone};
  return geometry::BlockProps(s);
}

geometry::BlockProps StairsProps() {
  // string_view sizes via strlen, excluding the null terminator; the
  // span must not include it or variant key matching breaks.
  std::string_view src = "facing=north,half=bottom,shape=straight";
  ir::BlockState s{
      .name = "minecraft:oak_stairs",
      .raw_properties = std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(src.data()), src.size()),
      .prop_encoding = ir::PropertyEncoding::kString,
  };
  return geometry::BlockProps(s);
}

// Props from a static "k=v,k=v" literal (span must exclude the null
// terminator, mirroring StairsProps above).
geometry::BlockProps Props(std::string_view kv) {
  ir::BlockState s{
      .name = "minecraft:test",
      .raw_properties = std::span<const std::byte>(
          reinterpret_cast<const std::byte*>(kv.data()), kv.size()),
      .prop_encoding = ir::PropertyEncoding::kString,
  };
  return geometry::BlockProps(s);
}

}  // namespace fschema::tests

#endif  // FSCHEMA_GEOMETRY_TESTS_RESOLVE_TEST_UTIL_H_
