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

#ifndef FSCHEMA_GEOMETRY_INTERNAL_MESH_APPEND_H_
#define FSCHEMA_GEOMETRY_INTERNAL_MESH_APPEND_H_

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

#include "fschema/geometry/types.h"

namespace fschema::geometry::internal {

// Raw quad writer over a Mesh. Replaces the ~26 per-quad push_backs the
// emit path used to pay (4 vertices x 6 interleaved vectors + 6
// indices, each with its own capacity check) with straight stores into
// a batch, plus one capacity decision per batch.
//
// Two addressing modes:
//
//  * Growing (Attach): appends at the vector ends through a stack
//    batch flushed with insert() every kBatchQuads quads. insert()
//    copies real data only -- resize()-based growth would value-init
//    (memset) the grown region first and double the write traffic of
//    the whole output. Used by the merge paths and the small-output
//    streaming branches, where the output size is not known up front;
//    a caller's up-front Reserve is honored by insert's capacity
//    check.
//  * Fixed (Window): writes through raw pointers into a pre-sized
//    slice [vertex_offset, vertex_offset + vertex_capacity) of already
//    sized vectors, from parallel workers with disjoint slices, and
//    never grows. Phase 1 of the two-phase streaming path counted
//    every quad exactly, so a full window means count/emit disagree
//    (a logic bug); the quad is dropped rather than corrupting a
//    neighbouring slice.
class MeshAppender {
 public:
  MeshAppender() = default;
  MeshAppender(const MeshAppender&) = delete;
  MeshAppender& operator=(const MeshAppender&) = delete;
  MeshAppender(MeshAppender&&) = default;
  MeshAppender& operator=(MeshAppender&&) = default;

  // Growing appender at the current end of `mesh`.
  [[nodiscard]] static MeshAppender Attach(Mesh& mesh) {
    MeshAppender a;
    a.mesh_ = &mesh;
    a.vertex_offset_ = mesh.VertexCount();
    a.index_offset_ = mesh.indices.size();
    a.growing_ = true;
    return a;
  }

  // Fixed window over vectors already sized for the whole pass.
  // `vertex_offset`/`index_offset` place the window; the capacities
  // bound it.
  [[nodiscard]] static MeshAppender Window(Mesh& mesh,
                                           std::size_t vertex_offset,
                                           std::size_t index_offset,
                                           std::size_t vertex_capacity,
                                           std::size_t index_capacity) {
    MeshAppender a;
    a.mesh_ = &mesh;
    a.vertex_offset_ = vertex_offset;
    a.index_offset_ = index_offset;
    a.growing_ = false;
    a.window_verts_ = vertex_capacity;
    a.window_idx_ = index_capacity;
    a.pos_ = mesh.positions.data() + vertex_offset * 3;
    a.nrm_ = mesh.normals.data() + vertex_offset * 3;
    a.uv_ = mesh.uvs.data() + vertex_offset * 2;
    a.ao_ = mesh.ao.data() + vertex_offset;
    a.tint_ = mesh.tints.data() + vertex_offset;
    a.idx_ = mesh.indices.data() + index_offset;
    return a;
  }

  // Appends one quad: 4 corner positions (12 floats), one normal
  // shared by the four vertices, 4 uv pairs (8 floats), 4 AO bytes and
  // one tint byte replicated per vertex. Index winding matches
  // PushQuadIndices: (0,2,1) (0,3,2) relative to the quad's base.
  void AppendQuad(const float* pos12,
                  const float* normal3,
                  const float* uv8,
                  const std::uint8_t* ao4,
                  std::uint8_t tint) {
    if (growing_) {
      AppendBatched(pos12, normal3, uv8, ao4, tint);
    } else {
      AppendWindowed(pos12, normal3, uv8, ao4, tint);
    }
  }

  // Flushes a growing appender's pending batch (fixed windows need no
  // finalization: the vectors were sized up front).
  void Finish() {
    if (growing_) Flush();
  }

  [[nodiscard]] std::size_t written_verts() const noexcept {
    return growing_ ? committed_verts_ + batch_ * 4 : written_verts_;
  }

 private:
  // ---- Fixed-window write: one bounds check, straight stores. ----
  void AppendWindowed(const float* pos12,
                      const float* normal3,
                      const float* uv8,
                      const std::uint8_t* ao4,
                      std::uint8_t tint) {
    if (window_verts_ - written_verts_ < 4 ||
        window_idx_ - written_idx_ < 6) {
      // Fixed window exhausted: count/emit mismatch. Drop rather than
      // overrun a neighbouring worker's slice.
      return;
    }
    const std::uint32_t base =
        static_cast<std::uint32_t>(vertex_offset_ + written_verts_);
    float* p = pos_;
    float* n = nrm_;
    float* u = uv_;
    for (int i = 0; i < 4; ++i) {
      p[0] = pos12[i * 3 + 0];
      p[1] = pos12[i * 3 + 1];
      p[2] = pos12[i * 3 + 2];
      p += 3;
      n[0] = normal3[0];
      n[1] = normal3[1];
      n[2] = normal3[2];
      n += 3;
      u[0] = uv8[i * 2 + 0];
      u[1] = uv8[i * 2 + 1];
      u += 2;
    }
    std::memcpy(ao_, ao4, 4);
    std::memset(tint_, tint, 4);
    idx_[0] = base + 0;
    idx_[1] = base + 2;
    idx_[2] = base + 1;
    idx_[3] = base + 0;
    idx_[4] = base + 3;
    idx_[5] = base + 2;
    pos_ += 12;
    nrm_ += 12;
    uv_ += 8;
    ao_ += 4;
    tint_ += 4;
    idx_ += 6;
    written_verts_ += 4;
    written_idx_ += 6;
  }

  // ---- Growing write: accumulate into the stack batch. ----
  void AppendBatched(const float* pos12,
                     const float* normal3,
                     const float* uv8,
                     const std::uint8_t* ao4,
                     std::uint8_t tint) {
    const std::size_t pv = batch_ * 12;
    std::memcpy(bpos_ + pv, pos12, 12 * sizeof(float));
    for (int i = 0; i < 4; ++i) {
      bnrm_[pv + i * 3 + 0] = normal3[0];
      bnrm_[pv + i * 3 + 1] = normal3[1];
      bnrm_[pv + i * 3 + 2] = normal3[2];
    }
    std::memcpy(buv_ + batch_ * 8, uv8, 8 * sizeof(float));
    std::memcpy(bao_ + batch_ * 4, ao4, 4);
    std::memset(btint_ + batch_ * 4, tint, 4);
    // Vertex bases are batch-local until the flush fixes them up
    // against the mesh end.
    const std::uint32_t base = static_cast<std::uint32_t>(batch_ * 4);
    std::uint32_t* idx = bidx_ + batch_ * 6;
    idx[0] = base + 0;
    idx[1] = base + 2;
    idx[2] = base + 1;
    idx[3] = base + 0;
    idx[4] = base + 3;
    idx[5] = base + 2;
    if (++batch_ == kBatchQuads) Flush();
  }

  // Commits the stack batch into the mesh with one insert() per
  // array: a single memcpy of real data each, and realloc growth
  // inside insert copies only the live prefix (no value-init pass
  // over the new region).
  void Flush() {
    if (batch_ == 0) return;
    Mesh& m = *mesh_;
    const std::size_t n = batch_;
    const std::uint32_t base =
        static_cast<std::uint32_t>(vertex_offset_ + committed_verts_);
    for (std::size_t i = 0; i < n * 6; ++i) {
      bidx_[i] += base;
    }
    m.positions.insert(m.positions.end(), bpos_, bpos_ + n * 12);
    m.normals.insert(m.normals.end(), bnrm_, bnrm_ + n * 12);
    m.uvs.insert(m.uvs.end(), buv_, buv_ + n * 8);
    m.ao.insert(m.ao.end(), bao_, bao_ + n * 4);
    m.tints.insert(m.tints.end(), btint_, btint_ + n * 4);
    m.indices.insert(m.indices.end(), bidx_, bidx_ + n * 6);
    committed_verts_ += n * 4;
    committed_idx_ += n * 6;
    batch_ = 0;
  }

  static constexpr std::size_t kBatchQuads = 64;

  Mesh* mesh_ = nullptr;
  bool growing_ = false;

  // Fixed-window state.
  std::size_t vertex_offset_ = 0;
  std::size_t index_offset_ = 0;
  std::size_t window_verts_ = 0;
  std::size_t window_idx_ = 0;
  std::size_t written_verts_ = 0;
  std::size_t written_idx_ = 0;
  float* pos_ = nullptr;
  float* nrm_ = nullptr;
  float* uv_ = nullptr;
  std::uint8_t* ao_ = nullptr;
  std::uint8_t* tint_ = nullptr;
  std::uint32_t* idx_ = nullptr;

  // Growing-mode state: quad batch on the stack plus the committed
  // prefix (vertex/index counts already inserted into the mesh).
  std::size_t batch_ = 0;
  std::size_t committed_verts_ = 0;
  std::size_t committed_idx_ = 0;
  float bpos_[kBatchQuads * 12];
  float bnrm_[kBatchQuads * 12];
  float buv_[kBatchQuads * 8];
  std::uint8_t bao_[kBatchQuads * 4];
  std::uint8_t btint_[kBatchQuads * 4];
  std::uint32_t bidx_[kBatchQuads * 6];
};

}  // namespace fschema::geometry::internal

#endif  // FSCHEMA_GEOMETRY_INTERNAL_MESH_APPEND_H_
