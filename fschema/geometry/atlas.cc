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

#include "fschema/geometry/atlas.h"

#include <cstring>
#include <utility>
#include <vector>

#include "fschema/base/error.h"

namespace fschema::geometry {

TextureAtlasBuilder::TextureAtlasBuilder(std::uint32_t max_extent,
                                         std::uint32_t padding)
    : max_extent_(max_extent == 0 ? 1 : max_extent),
      padding_(padding),
      canvas_width_(max_extent == 0 ? 1 : max_extent) {}

ParseResult<std::uint32_t> TextureAtlasBuilder::Insert(
    std::string_view texture_path,
    std::span<const std::uint8_t> rgba,
    std::uint32_t w,
    std::uint32_t h,
    std::uint32_t repeats_x,
    std::uint32_t repeats_y) {
  if (auto it = dedup_.find(std::string(texture_path)); it != dedup_.end()) {
    return it->second;
  }

  if (w == 0 || h == 0) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kAtlasOverflow,
                       std::string(texture_path) + " (zero width or height)",
                       0));
  }
  if (repeats_x == 0) repeats_x = 1;
  if (repeats_y == 0) repeats_y = 1;

  // Slot rect size: the texture tiled repeats_x * repeats_y.
  const std::uint32_t tiled_w = w * repeats_x;
  const std::uint32_t tiled_h = h * repeats_y;
  const std::uint32_t padded_w = tiled_w + 2 * padding_;
  const std::uint32_t padded_h = tiled_h + 2 * padding_;

  if (padded_w > canvas_width_) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kAtlasOverflow,
                       std::string(texture_path) + " (padded width " +
                           std::to_string(padded_w) + " exceeds canvas width " +
                           std::to_string(canvas_width_) + ")",
                       0));
  }

  std::uint32_t placement_x;
  std::uint32_t placement_y;

  if (current_row_x_ + padded_w <= canvas_width_) {
    placement_x = current_row_x_;
    placement_y = current_row_y_;
  } else {
    placement_x = 0;
    placement_y = current_row_y_ + current_row_height_;

    if (placement_y + padded_h > max_extent_) {
      return std::unexpected(
          ParseError::At(ParseError::Code::kAtlasOverflow,
                         std::string(texture_path) +
                             " (canvas height would exceed max_extent " +
                             std::to_string(max_extent_) + ")",
                         0));
    }

    current_row_x_ = 0;
    current_row_y_ = placement_y;
    current_row_height_ = 0;
  }

  if (current_row_y_ + padded_h > max_extent_) {
    return std::unexpected(ParseError::At(
        ParseError::Code::kAtlasOverflow,
        std::string(texture_path) + " (canvas height would exceed max_extent " +
            std::to_string(max_extent_) + ")",
        0));
  }

  PendingEntry entry;
  entry.placement_x = placement_x;
  entry.placement_y = placement_y;
  entry.src_w = w;
  entry.src_h = h;
  entry.width = tiled_w;
  entry.height = tiled_h;
  entry.repeats_x = repeats_x;
  entry.repeats_y = repeats_y;
  entry.pixels.assign(rgba.begin(), rgba.end());

  const std::uint32_t slot_index = static_cast<std::uint32_t>(pending_.size());
  pending_.push_back(std::move(entry));
  slots_.push_back(TextureAtlas::Slot{});
  dedup_.emplace(std::string(texture_path), slot_index);

  current_row_x_ += padded_w;
  if (padded_h > current_row_height_) current_row_height_ = padded_h;

  return slot_index;
}

TextureAtlas TextureAtlasBuilder::Finalize() && {
  TextureAtlas out;
  out.width = canvas_width_;
  out.height = current_row_y_ + current_row_height_;

  // Round height up to a power of 2 to be friendly to GL/GPU upload.
  if (out.height > 0) {
    std::uint32_t h = 1;
    while (h < out.height) h <<= 1;
    if (h <= max_extent_) out.height = h;
  }

  if (out.width == 0 || out.height == 0) return out;

  out.rgba.assign(static_cast<std::size_t>(out.width) *
                      static_cast<std::size_t>(out.height) * 4,
                  0);

  for (std::size_t i = 0; i < pending_.size(); ++i) {
    const auto& e = pending_[i];
    const std::uint32_t dst_x = e.placement_x + padding_;
    const std::uint32_t dst_y = e.placement_y + padding_;

    // Tiled fill: slot row `row` copies source row `row % src_h`
    // repeats_x times side by side; repeats 1x1 degenerates to the
    // plain single-texture copy.
    for (std::uint32_t row = 0; row < e.height; ++row) {
      const std::uint8_t* src =
          e.pixels.data() + ((row % e.src_h) * e.src_w * 4);
      std::uint8_t* dst =
          out.rgba.data() + ((dst_y + row) * out.width + dst_x) * 4;
      for (std::uint32_t r = 0; r < e.repeats_x; ++r) {
        std::memcpy(dst + r * e.src_w * 4, src, e.src_w * 4);
      }
    }

    TextureAtlas::Slot& slot = slots_[i];
    slot.u0 = static_cast<float>(dst_x) / static_cast<float>(out.width);
    slot.v0 = static_cast<float>(dst_y) / static_cast<float>(out.height);
    slot.u1 =
        static_cast<float>(dst_x + e.width) / static_cast<float>(out.width);
    slot.v1 =
        static_cast<float>(dst_y + e.height) / static_cast<float>(out.height);
    slot.repeats_x = e.repeats_x;
    slot.repeats_y = e.repeats_y;
  }

  out.slots = std::move(slots_);
  return out;
}

}  // namespace fschema::geometry
