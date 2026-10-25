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

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <new>
#include <sstream>
#include <string>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/geometry/internal/png_encode.h"
#include "fschema/geometry/tint.h"
#include "fschema/geometry/types.h"

namespace fschema::geometry {

namespace {

constexpr std::uint32_t kGlbMagic = 0x46546C67u;  // "glTF" little-endian
constexpr std::uint32_t kGlbVersion = 2u;
constexpr std::uint32_t kJsonChunkType = 0x4E4F534Au;  // "JSON"
constexpr std::uint32_t kBinChunkType = 0x004E4942u;   // "BIN\0"

// glTF component types.
constexpr std::uint32_t kComponentTypeFloat = 5126u;  // FLOAT
constexpr std::uint32_t kComponentTypeUByte = 5121u;  // UNSIGNED_BYTE
constexpr std::uint32_t kComponentTypeUInt = 5125u;   // UNSIGNED_INT

// glTF bufferView targets.
constexpr std::uint32_t kTargetArrayBuffer = 34962u;
constexpr std::uint32_t kTargetElementArrayBuffer = 34963u;

// glTF sampler constants.
constexpr std::uint32_t kFilterNearest = 9728u;
constexpr std::uint32_t kFilterLinearMipmapLinear = 9987u;
constexpr std::uint32_t kWrapRepeat = 10497u;

void AppendU32LE(std::vector<std::uint8_t>& out, std::uint32_t v) {
  out.push_back(static_cast<std::uint8_t>(v & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
  out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void AppendBytes(std::vector<std::uint8_t>& out,
                 const void* data,
                 std::size_t n) {
  const auto* p = static_cast<const std::uint8_t*>(data);
  out.insert(out.end(), p, p + n);
}

// Pad a buffer to a multiple of 4 bytes with the given filler byte.
void PadTo4(std::vector<std::uint8_t>& buf, std::uint8_t filler) {
  while (buf.size() % 4 != 0) buf.push_back(filler);
}

struct BufferView {
  std::size_t byte_offset;
  std::size_t byte_length;
  std::uint32_t target;  // 0 = no target (image data)
};

struct Accessor {
  std::uint32_t buffer_view;
  std::size_t byte_offset;  // offset within bufferView
  std::uint32_t component_type;
  std::uint32_t count;
  std::uint32_t type;  // 1=SCALAR, 2=VEC2, 3=VEC3, 4=VEC4
  bool normalized;
  std::array<float, 3> min{};
  std::array<float, 3> max{};
  bool has_min_max;
};

std::string_view AccessorTypeString(std::uint32_t t) {
  switch (t) {
    case 1:
      return "SCALAR";
    case 2:
      return "VEC2";
    case 3:
      return "VEC3";
    case 4:
      return "VEC4";
    default:
      return "SCALAR";
  }
}

struct PrimitiveData {
  std::uint32_t accessor_pos;
  std::uint32_t accessor_nrm;
  std::uint32_t accessor_uv;
  std::uint32_t accessor_col;
  std::uint32_t accessor_idx;
  std::uint32_t vertex_count;
  std::uint32_t index_count;
  bool has_geometry;
};

// Appends one mesh's attributes, accessors, and bufferViews to the BIN
// buffer. COLOR_0 packs TintTable::slots[tints[i]] into RGB and mesh.ao[i]
// into A (255 = identity tint / unoccluded).
PrimitiveData EmitMesh(const Mesh& mesh,
                       const TintTable& tint,
                       std::vector<std::uint8_t>& bin,
                       std::vector<BufferView>& bvs,
                       std::vector<Accessor>& accs) {
  PrimitiveData out{};
  if (mesh.positions.empty()) return out;  // no geometry
  out.has_geometry = true;
  out.vertex_count = static_cast<std::uint32_t>(mesh.VertexCount());
  out.index_count = static_cast<std::uint32_t>(mesh.indices.size());

  // POSITION (FLOAT, VEC3).
  {
    const std::size_t off = bin.size();
    AppendBytes(
        bin, mesh.positions.data(), mesh.positions.size() * sizeof(float));
    PadTo4(bin, 0);
    const std::uint32_t bv_idx = static_cast<std::uint32_t>(bvs.size());
    bvs.push_back(
        {off, mesh.positions.size() * sizeof(float), kTargetArrayBuffer});

    Accessor a{};
    a.buffer_view = bv_idx;
    a.byte_offset = 0;
    a.component_type = kComponentTypeFloat;
    a.count = out.vertex_count;
    a.type = 3;  // VEC3
    a.normalized = false;
    a.has_min_max = true;
    // Compute min/max for POSITION (spec-required for frustum culling).
    for (std::size_t i = 0; i + 2 < mesh.positions.size(); i += 3) {
      for (int k = 0; k < 3; ++k) {
        const float v = mesh.positions[i + k];
        if (v < a.min[k]) a.min[k] = v;
        if (v > a.max[k]) a.max[k] = v;
      }
    }
    out.accessor_pos = static_cast<std::uint32_t>(accs.size());
    accs.push_back(std::move(a));
  }

  // NORMAL (FLOAT, VEC3).
  {
    const std::size_t off = bin.size();
    AppendBytes(bin, mesh.normals.data(), mesh.normals.size() * sizeof(float));
    PadTo4(bin, 0);
    const std::uint32_t bv_idx = static_cast<std::uint32_t>(bvs.size());
    bvs.push_back(
        {off, mesh.normals.size() * sizeof(float), kTargetArrayBuffer});

    Accessor a{};
    a.buffer_view = bv_idx;
    a.byte_offset = 0;
    a.component_type = kComponentTypeFloat;
    a.count = out.vertex_count;
    a.type = 3;
    a.normalized = false;
    a.has_min_max = false;
    out.accessor_nrm = static_cast<std::uint32_t>(accs.size());
    accs.push_back(std::move(a));
  }

  // TEXCOORD_0 (FLOAT, VEC2).
  {
    const std::size_t off = bin.size();
    AppendBytes(bin, mesh.uvs.data(), mesh.uvs.size() * sizeof(float));
    PadTo4(bin, 0);
    const std::uint32_t bv_idx = static_cast<std::uint32_t>(bvs.size());
    bvs.push_back({off, mesh.uvs.size() * sizeof(float), kTargetArrayBuffer});

    Accessor a{};
    a.buffer_view = bv_idx;
    a.byte_offset = 0;
    a.component_type = kComponentTypeFloat;
    a.count = out.vertex_count;
    a.type = 2;  // VEC2
    a.normalized = false;
    a.has_min_max = false;
    out.accessor_uv = static_cast<std::uint32_t>(accs.size());
    accs.push_back(std::move(a));
  }

  // COLOR_0 (UNSIGNED_BYTE, VEC4, normalized).
  {
    std::vector<std::uint8_t> colors;
    colors.reserve(out.vertex_count * 4);
    for (std::uint32_t i = 0; i < out.vertex_count; ++i) {
      const std::uint8_t tint_slot = i < mesh.tints.size() ? mesh.tints[i] : 0;
      const std::array<std::uint8_t, 3>* tint_rgb = nullptr;
      if (tint_slot < TintTable::kSlotCount) {
        tint_rgb = &tint.slots[tint_slot];
      }
      colors.push_back(tint_rgb ? (*tint_rgb)[0] : 255);  // R
      colors.push_back(tint_rgb ? (*tint_rgb)[1] : 255);  // G
      colors.push_back(tint_rgb ? (*tint_rgb)[2] : 255);  // B
      colors.push_back(i < mesh.ao.size() ? mesh.ao[i]
                                          : static_cast<std::uint8_t>(255));
    }
    const std::size_t off = bin.size();
    AppendBytes(bin, colors.data(), colors.size());
    PadTo4(bin, 0);
    const std::uint32_t bv_idx = static_cast<std::uint32_t>(bvs.size());
    bvs.push_back({off, colors.size(), kTargetArrayBuffer});

    Accessor a{};
    a.buffer_view = bv_idx;
    a.byte_offset = 0;
    a.component_type = kComponentTypeUByte;
    a.count = out.vertex_count;
    a.type = 4;  // VEC4
    a.normalized = true;
    a.has_min_max = false;
    out.accessor_col = static_cast<std::uint32_t>(accs.size());
    accs.push_back(std::move(a));
  }

  // INDICES (UNSIGNED_INT, SCALAR).
  {
    const std::size_t off = bin.size();
    AppendBytes(
        bin, mesh.indices.data(), mesh.indices.size() * sizeof(std::uint32_t));
    PadTo4(bin, 0);
    const std::uint32_t bv_idx = static_cast<std::uint32_t>(bvs.size());
    bvs.push_back({off,
                   mesh.indices.size() * sizeof(std::uint32_t),
                   kTargetElementArrayBuffer});

    Accessor a{};
    a.buffer_view = bv_idx;
    a.byte_offset = 0;
    a.component_type = kComponentTypeUInt;
    a.count = out.index_count;
    a.type = 1;  // SCALAR
    a.normalized = false;
    a.has_min_max = false;
    out.accessor_idx = static_cast<std::uint32_t>(accs.size());
    accs.push_back(std::move(a));
  }

  return out;
}

// Emit the atlas as a PNG-encoded bufferView. Returns the bufferView
// index, or UINT32_MAX if the atlas is empty.
ParseResult<std::uint32_t> EmitAtlas(const TextureAtlas& atlas,
                                     std::vector<std::uint8_t>& bin,
                                     std::vector<BufferView>& bvs) {
  if (atlas.rgba.empty()) return UINT32_MAX;
  std::vector<std::uint8_t> png;
  try {
    png = internal::EncodePng(atlas.width, atlas.height, atlas.rgba);
  } catch (const std::bad_alloc&) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kGltfWriteFailed, "EmitAtlas: out of memory", 0));
  }
  const std::size_t off = bin.size();
  AppendBytes(bin, png.data(), png.size());
  PadTo4(bin, 0);
  const std::uint32_t bv_idx = static_cast<std::uint32_t>(bvs.size());
  bvs.push_back({off, png.size(), /*target=*/0});
  return bv_idx;
}

std::string FormatFloat(float v) {
  std::ostringstream ss;
  ss << v;
  return ss.str();
}

std::string FormatFloatArray(const float* p, std::size_t n) {
  std::string s = "[";
  for (std::size_t i = 0; i < n; ++i) {
    if (i) s += ",";
    s += FormatFloat(p[i]);
  }
  s += "]";
  return s;
}

std::string FormatAccessor(const Accessor& a) {
  std::string s = "{\"bufferView\":";
  s += std::to_string(a.buffer_view);
  s += ",\"byteOffset\":";
  s += std::to_string(a.byte_offset);
  s += ",\"componentType\":";
  s += std::to_string(a.component_type);
  s += ",\"count\":";
  s += std::to_string(a.count);
  s += ",\"type\":\"";
  s += AccessorTypeString(a.type);
  s += "\"";
  if (a.normalized) s += ",\"normalized\":true";
  if (a.has_min_max) {
    s += ",\"min\":" + FormatFloatArray(a.min.data(), 3);
    s += ",\"max\":" + FormatFloatArray(a.max.data(), 3);
  }
  s += "}";
  return s;
}

std::string FormatBufferView(const BufferView& bv) {
  std::string s = "{\"buffer\":0,\"byteOffset\":";
  s += std::to_string(bv.byte_offset);
  s += ",\"byteLength\":";
  s += std::to_string(bv.byte_length);
  if (bv.target != 0) {
    s += ",\"target\":";
    s += std::to_string(bv.target);
  }
  s += "}";
  return s;
}

}  // namespace

ParseResult<ByteBuffer> ExportGlb(const TessellationResult& result,
                                  const TintTable& tint) {
  std::vector<std::uint8_t> bin;
  std::vector<BufferView> bvs;
  std::vector<Accessor> accs;

  // Both meshes share one BIN buffer.
  PrimitiveData opaque = EmitMesh(result.opaque_mesh, tint, bin, bvs, accs);
  PrimitiveData transparent =
      EmitMesh(result.transparent_mesh, tint, bin, bvs, accs);

  auto atlas_bv_res = EmitAtlas(result.atlas, bin, bvs);
  if (!atlas_bv_res) return std::unexpected(atlas_bv_res.error());
  const std::uint32_t atlas_bv = *atlas_bv_res;

  std::string json = "{";
  json +=
      "\"asset\":{\"version\":\"2.0\",\"generator\":\"FastSchema geometry "
      "subsystem\"}";
  json += ",\"scene\":0";
  json += ",\"scenes\":[{\"nodes\":[0]}]";
  json += ",\"nodes\":[{\"mesh\":0}]";

  // One mesh with up to two primitives (opaque, transparent).
  json += ",\"meshes\":[{\"primitives\":[";
  bool first = true;
  if (opaque.has_geometry) {
    json += "{\"attributes\":{\"POSITION\":";
    json += std::to_string(opaque.accessor_pos);
    json += ",\"NORMAL\":";
    json += std::to_string(opaque.accessor_nrm);
    json += ",\"TEXCOORD_0\":";
    json += std::to_string(opaque.accessor_uv);
    json += ",\"COLOR_0\":";
    json += std::to_string(opaque.accessor_col);
    json += "},\"indices\":";
    json += std::to_string(opaque.accessor_idx);
    json += ",\"material\":0}";
    first = false;
  }
  if (transparent.has_geometry) {
    if (!first) json += ",";
    json += "{\"attributes\":{\"POSITION\":";
    json += std::to_string(transparent.accessor_pos);
    json += ",\"NORMAL\":";
    json += std::to_string(transparent.accessor_nrm);
    json += ",\"TEXCOORD_0\":";
    json += std::to_string(transparent.accessor_uv);
    json += ",\"COLOR_0\":";
    json += std::to_string(transparent.accessor_col);
    json += "},\"indices\":";
    json += std::to_string(transparent.accessor_idx);
    json += ",\"material\":1}";
  }
  json += "]}]";

  // Material 0 = OPAQUE, 1 = BLEND.
  json += ",\"materials\":[";
  json +=
      "{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},"
      "\"metallicFactor\":0,\"roughnessFactor\":1},"
      "\"alphaMode\":\"OPAQUE\"}";
  if (transparent.has_geometry) {
    json +=
        ",{\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0},"
        "\"metallicFactor\":0,\"roughnessFactor\":1},"
        "\"alphaMode\":\"BLEND\"}";
  }
  json += "]";

  json += ",\"buffers\":[{\"byteLength\":";
  json += std::to_string(bin.size());
  json += "}]";

  json += ",\"bufferViews\":[";
  for (std::size_t i = 0; i < bvs.size(); ++i) {
    if (i) json += ",";
    json += FormatBufferView(bvs[i]);
  }
  json += "]";

  json += ",\"accessors\":[";
  for (std::size_t i = 0; i < accs.size(); ++i) {
    if (i) json += ",";
    json += FormatAccessor(accs[i]);
  }
  json += "]";

  // Atlas image, when present.
  if (atlas_bv != UINT32_MAX) {
    json += ",\"images\":[{\"bufferView\":";
    json += std::to_string(atlas_bv);
    json += ",\"mimeType\":\"image/png\"}]";
    json += ",\"textures\":[{\"source\":0}]";
    json += ",\"samplers\":[{\"magFilter\":";
    json += std::to_string(kFilterNearest);
    json += ",\"minFilter\":";
    json += std::to_string(kFilterLinearMipmapLinear);
    json += ",\"wrapS\":";
    json += std::to_string(kWrapRepeat);
    json += ",\"wrapT\":";
    json += std::to_string(kWrapRepeat);
    json += "}]";
  }

  json += "}";

  // Pad JSON to a 4-byte boundary with spaces (spec requirement).
  while (json.size() % 4 != 0) json.push_back(' ');

  ByteBuffer out;
  const std::uint32_t total_length = 12 + 8 +
                                     static_cast<std::uint32_t>(json.size()) +
                                     8 + static_cast<std::uint32_t>(bin.size());

  // Header (12 bytes).
  AppendU32LE(out.bytes, kGlbMagic);
  AppendU32LE(out.bytes, kGlbVersion);
  AppendU32LE(out.bytes, total_length);

  // JSON chunk (8-byte header + data).
  AppendU32LE(out.bytes, static_cast<std::uint32_t>(json.size()));
  AppendU32LE(out.bytes, kJsonChunkType);
  out.bytes.insert(out.bytes.end(), json.begin(), json.end());

  // BIN chunk (8-byte header + data; already 4-byte padded by EmitMesh).
  AppendU32LE(out.bytes, static_cast<std::uint32_t>(bin.size()));
  AppendU32LE(out.bytes, kBinChunkType);
  out.bytes.insert(out.bytes.end(), bin.begin(), bin.end());

  return out;
}

ParseResult<std::pair<ByteBuffer, ByteBuffer>> ExportObj(
    const TessellationResult& result,
    std::string_view name) {
  const std::string atlas_name = std::string(name) + "_atlas.png";
  const std::string mtl_name = std::string(name) + ".mtl";

  std::string mtl_str;
  mtl_str += "# Generated by FastSchema\n";
  mtl_str += "newmtl fschema_opaque\n";
  mtl_str += "Kd 1.0 1.0 1.0\n";
  mtl_str += "map_Kd " + atlas_name + "\n";
  mtl_str += "\n";
  mtl_str += "newmtl fschema_transparent\n";
  mtl_str += "Kd 1.0 1.0 1.0\n";
  mtl_str += "map_Kd " + atlas_name + "\n";
  mtl_str += "Tr 0.5\n";  // semi-transparent
  mtl_str += "\n";

  std::string obj_str;
  obj_str += "# Generated by FastSchema geometry subsystem\n";
  obj_str += "mtllib " + mtl_name + "\n";

  auto Fmt = [](float f) {
    std::ostringstream ss;
    ss << std::fixed << std::setprecision(6) << f;
    return ss.str();
  };

  auto WriteMesh = [&](const Mesh& mesh, const char* mtl_name_str) {
    if (mesh.positions.empty()) return;
    for (std::size_t i = 0; i < mesh.positions.size(); i += 3) {
      obj_str += "v " + Fmt(mesh.positions[i]) + " " +
                 Fmt(mesh.positions[i + 1]) + " " + Fmt(mesh.positions[i + 2]) +
                 "\n";
    }
    for (std::size_t i = 0; i < mesh.uvs.size(); i += 2) {
      obj_str += "vt " + Fmt(mesh.uvs[i]) + " " + Fmt(mesh.uvs[i + 1]) + "\n";
    }
    for (std::size_t i = 0; i < mesh.normals.size(); i += 3) {
      obj_str += "vn " + Fmt(mesh.normals[i]) + " " + Fmt(mesh.normals[i + 1]) +
                 " " + Fmt(mesh.normals[i + 2]) + "\n";
    }
    obj_str += "usemtl ";
    obj_str += mtl_name_str;
    obj_str += "\n";
    for (std::size_t i = 0; i < mesh.indices.size(); i += 3) {
      const std::uint32_t a = mesh.indices[i] + 1;
      const std::uint32_t b = mesh.indices[i + 1] + 1;
      const std::uint32_t c = mesh.indices[i + 2] + 1;
      obj_str += "f " + std::to_string(a) + "/" + std::to_string(a) + "/" +
                 std::to_string(a) + " " + std::to_string(b) + "/" +
                 std::to_string(b) + "/" + std::to_string(b) + " " +
                 std::to_string(c) + "/" + std::to_string(c) + "/" +
                 std::to_string(c) + "\n";
    }
  };

  WriteMesh(result.opaque_mesh, "fschema_opaque");

  if (!result.transparent_mesh.positions.empty()) {
    Mesh tmp = result.transparent_mesh;
    const std::uint32_t offset =
        static_cast<std::uint32_t>(result.opaque_mesh.VertexCount());
    for (auto& idx : tmp.indices) idx += offset;
    WriteMesh(tmp, "fschema_transparent");
  }

  ByteBuffer obj_bb, mtl_bb;
  obj_bb.bytes.assign(obj_str.begin(), obj_str.end());
  mtl_bb.bytes.assign(mtl_str.begin(), mtl_str.end());
  return std::make_pair(std::move(obj_bb), std::move(mtl_bb));
}

RawMesh ExportRaw(const TessellationResult& result) {
  RawMesh out;
  const Mesh& op = result.opaque_mesh;
  const Mesh& tr = result.transparent_mesh;

  auto CopyBytes = [](ByteBuffer& dst, const auto& src) {
    using T = typename std::decay_t<decltype(src)>::value_type;
    const auto* begin = reinterpret_cast<const std::uint8_t*>(src.data());
    const auto* end =
        reinterpret_cast<const std::uint8_t*>(src.data() + src.size());
    dst.bytes.assign(begin, end);
  };

  CopyBytes(out.positions, op.positions);
  if (!tr.positions.empty()) {
    const auto sz = out.positions.bytes.size();
    out.positions.bytes.resize(sz + tr.positions.size() * sizeof(float));
    std::memcpy(out.positions.bytes.data() + sz,
                tr.positions.data(),
                tr.positions.size() * sizeof(float));
  }

  CopyBytes(out.normals, op.normals);
  if (!tr.normals.empty()) {
    const auto sz = out.normals.bytes.size();
    out.normals.bytes.resize(sz + tr.normals.size() * sizeof(float));
    std::memcpy(out.normals.bytes.data() + sz,
                tr.normals.data(),
                tr.normals.size() * sizeof(float));
  }

  CopyBytes(out.uvs, op.uvs);
  if (!tr.uvs.empty()) {
    const auto sz = out.uvs.bytes.size();
    out.uvs.bytes.resize(sz + tr.uvs.size() * sizeof(float));
    std::memcpy(out.uvs.bytes.data() + sz,
                tr.uvs.data(),
                tr.uvs.size() * sizeof(float));
  }

  CopyBytes(out.indices, op.indices);
  if (!tr.indices.empty()) {
    const auto sz = out.indices.bytes.size();
    out.indices.bytes.resize(sz + tr.indices.size() * sizeof(std::uint32_t));
    std::memcpy(out.indices.bytes.data() + sz,
                tr.indices.data(),
                tr.indices.size() * sizeof(std::uint32_t));
  }

  out.vertex_count =
      static_cast<std::uint32_t>(op.VertexCount() + tr.VertexCount());
  out.index_count =
      static_cast<std::uint32_t>(op.indices.size() + tr.indices.size());

  return out;
}

}  // namespace fschema::geometry
