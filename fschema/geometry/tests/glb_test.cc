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

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/export.h"
#include "fschema/geometry/internal/png_decode.h"
#include "fschema/geometry/resource_pack.h"
#include "fschema/geometry/tessellate.h"
#include "fschema/geometry/tests/geometry_test_util.h"
#include "fschema/geometry/types.h"
#include "fschema/memory/arena.h"
#include "fschema/memory/uninit_buffer.h"
#include "fschema/third_party/simdjson/simdjson.h"

namespace {

using fschema::ToString;
using fschema::geometry::ByteBuffer;
using fschema::geometry::ExportGlb;
using fschema::geometry::ResourcePack;
using fschema::geometry::TessellateRegion;
using fschema::geometry::TessellationOptions;
using fschema::geometry::TessellationResult;
using fschema::tests::ResourcePackFixture;
using fschema::tests::StoneRegion;
using fschema::tests::TempDir;

// Parse the JSON chunk of a GLB file using simdjson. Returns the parsed
// dom::element root, or fails the test if the JSON is malformed.
//
// simdjson requires SIMDJSON_PADDING bytes of padding after the JSON
// content for safe SIMD reads near the end of the buffer. The GLB
// buffer does NOT have this padding (BIN chunk follows immediately),
// so we copy the JSON chunk into a simdjson::padded_string before
// calling parser.parse().
simdjson::dom::element ParseGlbJson(const ByteBuffer& glb,
                                    simdjson::dom::parser& parser) {
  // Header: 12 bytes (magic, version, length).
  EXPECT_EQ(glb.bytes[0], 'g');
  EXPECT_EQ(glb.bytes[1], 'l');
  EXPECT_EQ(glb.bytes[2], 'T');
  EXPECT_EQ(glb.bytes[3], 'F');

  // Chunk 0 header: 8 bytes (chunkLength, chunkType "JSON").
  const std::size_t json_offset = 12;
  std::uint32_t json_len = 0;
  std::memcpy(&json_len, glb.bytes.data() + json_offset, 4);
  EXPECT_EQ(glb.bytes[json_offset + 4], 'J');
  EXPECT_EQ(glb.bytes[json_offset + 5], 'S');
  EXPECT_EQ(glb.bytes[json_offset + 6], 'O');
  EXPECT_EQ(glb.bytes[json_offset + 7], 'N');

  // simdjson::padded_string copies the bytes into a buffer with
  // SIMDJSON_PADDING bytes of trailing zero padding, satisfying the
  // parser's safety requirement.
  simdjson::padded_string padded(
      reinterpret_cast<const char*>(glb.bytes.data() + json_offset + 8),
      json_len);

  auto result = parser.parse(padded);
  EXPECT_EQ(result.error(), simdjson::SUCCESS)
      << "JSON chunk did not parse: "
      << simdjson::error_message(result.error());
  // If parse failed, dereferencing result.value() would throw. Use
  // .value() only on success.
  if (result.error() != simdjson::SUCCESS) {
    // Return an empty object to avoid crashing subsequent EXPECT_EQs.
    static const simdjson::padded_string kEmptyJson("{}", 2);
    return parser.parse(kEmptyJson).value();
  }
  return result.value();
}

TEST(GlbExportTest, HeaderIsCorrectForStoneCube) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(2, 2, 2);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value()) << ToString(tess.error());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value()) << ToString(glb.error());

  // 12-byte header.
  ASSERT_GE(glb->bytes.size(), 12u);
  EXPECT_EQ(glb->bytes[0], 'g');
  EXPECT_EQ(glb->bytes[1], 'l');
  EXPECT_EQ(glb->bytes[2], 'T');
  EXPECT_EQ(glb->bytes[3], 'F');

  std::uint32_t version = 0, total_length = 0;
  std::memcpy(&version, glb->bytes.data() + 4, 4);
  std::memcpy(&total_length, glb->bytes.data() + 8, 4);
  EXPECT_EQ(version, 2u);
  EXPECT_EQ(total_length, glb->bytes.size())
      << "total_length header must match actual file size";
}

TEST(GlbExportTest, JsonChunkParsesAndHasExpectedStructure) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(2, 2, 2);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  simdjson::dom::parser parser;
  simdjson::dom::element root = ParseGlbJson(*glb, parser);

  // asset.version == "2.0"
  std::string_view asset_version;
  EXPECT_EQ(root["asset"]["version"].get_string().get(asset_version),
            simdjson::SUCCESS);
  EXPECT_EQ(asset_version, "2.0");

  // scene == 0
  std::int64_t scene_idx = 0;
  EXPECT_EQ(root["scene"].get_int64().get(scene_idx), simdjson::SUCCESS);
  EXPECT_EQ(scene_idx, 0);

  // scenes has 1 entry with nodes=[0]
  simdjson::dom::array scenes = root["scenes"].get_array();
  EXPECT_EQ(scenes.size(), 1u);
  simdjson::dom::array scene_nodes = scenes.at(0)["nodes"].get_array();
  EXPECT_EQ(scene_nodes.size(), 1u);

  // nodes has 1 entry
  simdjson::dom::array nodes = root["nodes"].get_array();
  EXPECT_EQ(nodes.size(), 1u);
  std::int64_t mesh_idx = 0;
  EXPECT_EQ(nodes.at(0)["mesh"].get_int64().get(mesh_idx), simdjson::SUCCESS);
  EXPECT_EQ(mesh_idx, 0);

  // meshes has 1 entry with 1 primitive
  simdjson::dom::array meshes = root["meshes"].get_array();
  EXPECT_EQ(meshes.size(), 1u);
  simdjson::dom::array prims = meshes.at(0)["primitives"].get_array();
  EXPECT_EQ(prims.size(), 1u);

  // Primitive has 4 vertex attributes + indices + material
  simdjson::dom::object attrs = prims.at(0)["attributes"].get_object();
  EXPECT_EQ(attrs.size(), 4u);
  EXPECT_EQ(prims.at(0)["indices"].error(), simdjson::SUCCESS);
  EXPECT_EQ(prims.at(0)["material"].error(), simdjson::SUCCESS);
}

TEST(GlbExportTest, BufferCountIsOne) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(2, 2, 2);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  simdjson::dom::parser parser;
  simdjson::dom::element root = ParseGlbJson(*glb, parser);

  simdjson::dom::array buffers = root["buffers"].get_array();
  EXPECT_EQ(buffers.size(), 1u);
  std::int64_t byte_length = 0;
  EXPECT_EQ(buffers.at(0)["byteLength"].get_int64().get(byte_length),
            simdjson::SUCCESS);
  EXPECT_GT(byte_length, 0);
}

TEST(GlbExportTest, AccessorCountMatchesVertexCount) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(2, 2, 2);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  // 2x2x2 stone cube: 24 visible faces, 24*4=96 vertices, 24*2=48 triangles,
  // 48*3=144 indices.
  EXPECT_EQ(tess->opaque_mesh.VertexCount(), 96u);
  EXPECT_EQ(tess->opaque_mesh.indices.size(), 144u);

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  simdjson::dom::parser parser;
  simdjson::dom::element root = ParseGlbJson(*glb, parser);
  simdjson::dom::array accessors = root["accessors"].get_array();
  // 4 vertex attrs (POSITION, NORMAL, TEXCOORD_0, COLOR_0) + 1 indices
  EXPECT_EQ(accessors.size(), 5u);

  // POSITION accessor: count == 96, type == "VEC3"
  std::int64_t pos_count = 0;
  EXPECT_EQ(accessors.at(0)["count"].get_int64().get(pos_count),
            simdjson::SUCCESS);
  EXPECT_EQ(pos_count, 96);
  std::string_view pos_type;
  EXPECT_EQ(accessors.at(0)["type"].get_string().get(pos_type),
            simdjson::SUCCESS);
  EXPECT_EQ(pos_type, "VEC3");

  // INDICES accessor (last one): count == 144, type == "SCALAR"
  std::int64_t idx_count = 0;
  EXPECT_EQ(accessors.at(4)["count"].get_int64().get(idx_count),
            simdjson::SUCCESS);
  EXPECT_EQ(idx_count, 144);
  std::string_view idx_type;
  EXPECT_EQ(accessors.at(4)["type"].get_string().get(idx_type),
            simdjson::SUCCESS);
  EXPECT_EQ(idx_type, "SCALAR");
}

TEST(GlbExportTest, MaterialIsOpaqueWithAlphaMode) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  simdjson::dom::parser parser;
  simdjson::dom::element root = ParseGlbJson(*glb, parser);
  simdjson::dom::array materials = root["materials"].get_array();
  EXPECT_EQ(materials.size(), 1u);

  std::string_view alpha_mode;
  EXPECT_EQ(materials.at(0)["alphaMode"].get_string().get(alpha_mode),
            simdjson::SUCCESS);
  EXPECT_EQ(alpha_mode, "OPAQUE");

  // pbrMetallicRoughness.baseColorTexture.index == 0
  std::int64_t tex_idx = 0;
  EXPECT_EQ(materials.at(0)["pbrMetallicRoughness"]["baseColorTexture"]["index"]
                .get_int64()
                .get(tex_idx),
            simdjson::SUCCESS);
  EXPECT_EQ(tex_idx, 0);
}

TEST(GlbExportTest, ImageAndTextureForAtlasArePresent) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  simdjson::dom::parser parser;
  simdjson::dom::element root = ParseGlbJson(*glb, parser);

  simdjson::dom::array images = root["images"].get_array();
  EXPECT_EQ(images.size(), 1u);
  std::string_view mime;
  EXPECT_EQ(images.at(0)["mimeType"].get_string().get(mime), simdjson::SUCCESS);
  EXPECT_EQ(mime, "image/png");

  simdjson::dom::array textures = root["textures"].get_array();
  EXPECT_EQ(textures.size(), 1u);
  std::int64_t source_idx = 0;
  EXPECT_EQ(textures.at(0)["source"].get_int64().get(source_idx),
            simdjson::SUCCESS);
  EXPECT_EQ(source_idx, 0u);
}

TEST(GlbExportTest, BinChunkContainsAtlasBytes) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  // Extract the BIN chunk.
  const std::size_t json_offset = 12;
  std::uint32_t json_len = 0;
  std::memcpy(&json_len, glb->bytes.data() + json_offset, 4);
  // JSON chunk is padded to 4 bytes, so the BIN chunk header starts at
  // 12 + 8 + ((json_len + 3) & ~3).
  const std::size_t bin_chunk_header_offset =
      12 + 8 + ((json_len + 3) & ~std::size_t(3));
  ASSERT_LT(bin_chunk_header_offset + 8, glb->bytes.size());
  EXPECT_EQ(glb->bytes[bin_chunk_header_offset + 4], 'B');
  EXPECT_EQ(glb->bytes[bin_chunk_header_offset + 5], 'I');
  EXPECT_EQ(glb->bytes[bin_chunk_header_offset + 6], 'N');
  EXPECT_EQ(glb->bytes[bin_chunk_header_offset + 7], '\0');

  std::uint32_t bin_len = 0;
  std::memcpy(&bin_len, glb->bytes.data() + bin_chunk_header_offset, 4);
  // BIN chunk holds: PNG-encoded atlas (~1100 bytes for 16x16),
  // 24 vertices * (12+12+8+4) = 864 bytes, 36 indices * 4 = 144 bytes.
  EXPECT_GT(bin_len, 2000u);
}

TEST(GlbExportTest, FileRoundTripReadsBackIdenticalBytes) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  // Write to a temp file, read back, compare bytes.
  TempDir tmp;
  const auto path = tmp.Get() / "out.glb";
  {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(f.good());
    f.write(reinterpret_cast<const char*>(glb->bytes.data()),
            static_cast<std::streamsize>(glb->bytes.size()));
    ASSERT_TRUE(f.good());
  }
  std::vector<std::uint8_t> read_back;
  {
    std::ifstream f(path, std::ios::binary);
    ASSERT_TRUE(f.good());
    f.seekg(0, std::ios::end);
    const std::streamsize size = f.tellg();
    f.seekg(0, std::ios::beg);
    read_back.resize(static_cast<std::size_t>(size));
    f.read(reinterpret_cast<char*>(read_back.data()), size);
    ASSERT_TRUE(f.good());
  }
  EXPECT_EQ(read_back, glb->bytes);
}

TEST(GlbExportTest, RepeatedExportProducesStableBytes) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto g1 = ExportGlb(*tess);
  auto g2 = ExportGlb(*tess);
  ASSERT_TRUE(g1.has_value());
  ASSERT_TRUE(g2.has_value());
  EXPECT_EQ(g1->bytes, g2->bytes);
}

TEST(GlbExportTest, AtlasImageRoundTripsThroughPngDecoder) {
  ResourcePackFixture fx;
  auto pack = ResourcePack::Load(fx.root.Get().generic_string());
  ASSERT_TRUE(pack.has_value());
  StoneRegion sr(1, 1, 1);

  auto tess =
      TessellateRegion(sr.region, *sr.arena, **pack, TessellationOptions{});
  ASSERT_TRUE(tess.has_value());

  auto glb = ExportGlb(*tess);
  ASSERT_TRUE(glb.has_value());

  simdjson::dom::parser parser;
  simdjson::dom::element root = ParseGlbJson(*glb, parser);

  simdjson::dom::array images = root["images"].get_array();
  ASSERT_EQ(images.size(), 1u);
  std::int64_t image_bv_idx = -1;
  ASSERT_EQ(images.at(0)["bufferView"].get_int64().get(image_bv_idx),
            simdjson::SUCCESS);
  ASSERT_GE(image_bv_idx, 0);

  simdjson::dom::array buffer_views = root["bufferViews"].get_array();
  simdjson::dom::element bv =
      buffer_views.at(static_cast<std::size_t>(image_bv_idx));
  std::int64_t byte_offset = 0, byte_length = 0;
  ASSERT_EQ(bv["byteOffset"].get_int64().get(byte_offset), simdjson::SUCCESS);
  ASSERT_EQ(bv["byteLength"].get_int64().get(byte_length), simdjson::SUCCESS);
  ASSERT_GT(byte_length, 0);

  std::uint32_t json_len = 0;
  std::memcpy(&json_len, glb->bytes.data() + 12, 4);
  const std::size_t bin_payload_off =
      12 + 8 + ((json_len + 3) & ~std::size_t(3)) + 8;

  std::span<const std::uint8_t> png_span(
      glb->bytes.data() + bin_payload_off + byte_offset,
      static_cast<std::size_t>(byte_length));

  std::unique_ptr<std::vector<std::uint8_t>> owned;
  auto decoded = fschema::geometry::internal::DecodePng(png_span, owned);
  ASSERT_TRUE(decoded.has_value()) << ToString(decoded.error());

  EXPECT_EQ(decoded->width, tess->atlas.width);
  EXPECT_EQ(decoded->height, tess->atlas.height);
  ASSERT_EQ(decoded->rgba.size(), tess->atlas.rgba.size());
  EXPECT_EQ(
      std::memcmp(
          decoded->rgba.data(), tess->atlas.rgba.data(), decoded->rgba.size()),
      0);
}

}  // namespace
