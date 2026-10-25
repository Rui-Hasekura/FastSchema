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

#include "fschema/geometry/export.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tessellate.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/types.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"

namespace {

using fschema::ToString;
using fschema::geometry::ByteBuffer;
using fschema::geometry::ExportObj;
using fschema::geometry::ExportRaw;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellationOptions;
using fschema::geometry::TessellationResult;
using fschema::tests::ResourcePackFixture;
using fschema::tests::StoneRegion;

std::string_view AsString(const ByteBuffer& bb) {
  return std::string_view(reinterpret_cast<const char*>(bb.bytes.data()),
                          bb.bytes.size());
}

TEST(ExportObjTest, ObjContainsExpectedLines) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto [obj, mtl] = *ExportObj(*tess, "test");
  const auto obj_str = AsString(obj);

  // Must contain OBJ directives.
  EXPECT_NE(obj_str.find("v "), std::string_view::npos);   // vertex positions
  EXPECT_NE(obj_str.find("vt "), std::string_view::npos);  // texture coords
  EXPECT_NE(obj_str.find("vn "), std::string_view::npos);  // normals
  EXPECT_NE(obj_str.find("f "), std::string_view::npos);   // faces
  EXPECT_NE(obj_str.find("mtllib test.mtl"), std::string_view::npos);
  EXPECT_NE(obj_str.find("usemtl fschema_opaque"), std::string_view::npos);
}

TEST(ExportObjTest, ObjIndicesAreOneBased) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto [obj, mtl] = *ExportObj(*tess, "test");
  const auto obj_str = AsString(obj);

  // OBJ is 1-indexed. The first face line should reference vertex 1.
  // Our internal indices are 0-based, so the first face should be:
  // f 1/1/1 2/2/2 3/3/3
  EXPECT_NE(obj_str.find("f 1/1/1 "), std::string_view::npos);
}

TEST(ExportObjTest, MtlReferencesAtlasTexture) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto [obj, mtl] = *ExportObj(*tess, "mybuild");
  const auto mtl_str = AsString(mtl);

  EXPECT_NE(mtl_str.find("newmtl fschema_opaque"), std::string_view::npos);
  EXPECT_NE(mtl_str.find("map_Kd mybuild_atlas.png"), std::string_view::npos);
}

TEST(ExportObjTest, ObjVertexCountMatchesMesh) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto [obj, mtl] = *ExportObj(*tess, "test");
  const auto obj_str = AsString(obj);

  // Count "v " lines (vertex positions). Should match mesh vertex count.
  std::size_t v_count = 0;
  std::size_t pos = 0;
  while ((pos = obj_str.find("\nv ", pos)) != std::string_view::npos) {
    ++v_count;
    pos += 3;
  }
  // Also check the first line (no leading newline).
  if (obj_str.substr(0, 2) == "v ") ++v_count;

  EXPECT_EQ(v_count, tess->opaque_mesh.VertexCount());
}

TEST(ExportObjTest, EmptyMeshProducesValidObj) {
  TessellationResult empty;
  auto [obj, mtl] = *ExportObj(empty, "empty");
  const auto obj_str = AsString(obj);

  // Empty mesh should still have the header and mtllib line.
  EXPECT_NE(obj_str.find("mtllib empty.mtl"), std::string_view::npos);
  // But no vertices or faces.
  EXPECT_EQ(obj_str.find("v 0"), std::string_view::npos);
}

TEST(ExportRawTest, RawByteCountsMatchMesh) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto raw = ExportRaw(*tess);

  const auto vc = tess->opaque_mesh.VertexCount();
  const auto ic = tess->opaque_mesh.indices.size();

  EXPECT_EQ(raw.vertex_count, vc);
  EXPECT_EQ(raw.index_count, ic);

  // positions: vc * 3 floats * 4 bytes
  EXPECT_EQ(raw.positions.bytes.size(), vc * 3 * sizeof(float));
  // normals: same
  EXPECT_EQ(raw.normals.bytes.size(), vc * 3 * sizeof(float));
  // uvs: vc * 2 floats * 4 bytes
  EXPECT_EQ(raw.uvs.bytes.size(), vc * 2 * sizeof(float));
  // indices: ic * 4 bytes (uint32)
  EXPECT_EQ(raw.indices.bytes.size(), ic * sizeof(std::uint32_t));
}

TEST(ExportRawTest, RawPositionsAreCorrectFloats) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto raw = ExportRaw(*tess);

  // Read first 3 floats from raw positions, compare to mesh.positions.
  float raw_pos[3];
  std::memcpy(raw_pos, raw.positions.bytes.data(), sizeof(raw_pos));
  EXPECT_FLOAT_EQ(raw_pos[0], tess->opaque_mesh.positions[0]);
  EXPECT_FLOAT_EQ(raw_pos[1], tess->opaque_mesh.positions[1]);
  EXPECT_FLOAT_EQ(raw_pos[2], tess->opaque_mesh.positions[2]);
}

TEST(ExportRawTest, EmptyMeshProducesEmptyRaw) {
  TessellationResult empty;
  auto raw = ExportRaw(empty);
  EXPECT_EQ(raw.vertex_count, 0u);
  EXPECT_EQ(raw.index_count, 0u);
  EXPECT_TRUE(raw.positions.bytes.empty());
  EXPECT_TRUE(raw.indices.bytes.empty());
}

}  // namespace
