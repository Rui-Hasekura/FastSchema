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

#include "fschema/geometry/internal/png_decode.h"

#include <cstring>

#define WUFFS_IMPLEMENTATION
#define WUFFS_CONFIG__MODULES
#define WUFFS_CONFIG__MODULE__BASE
#define WUFFS_CONFIG__MODULE__ADLER32
#define WUFFS_CONFIG__MODULE__CRC32
#define WUFFS_CONFIG__MODULE__DEFLATE
#define WUFFS_CONFIG__MODULE__ZLIB
#define WUFFS_CONFIG__MODULE__PNG
#include "fschema/third_party/wuffs/wuffs-v0.4.c"

namespace fschema::geometry::internal {

namespace {

// Hard upper bound on the pixel buffer (256 MiB); vanilla textures are
// 16x16 RGBA = 1 KiB, and Wuffs itself rejects dimensions above 0xFF_FFFF,
// so this only guards against pathological sizes.
constexpr std::uint64_t kMaxPixelBytes = 256ull * 1024 * 1024;

[[nodiscard]] ParseError StatusToError(const wuffs_base__status* s) {
  const char* msg = wuffs_base__status__message(s);
  return ParseError::At(ParseError::Code::kTextureDecodeFailed,
                        std::string(msg ? msg : "(unknown wuffs status)"),
                        0);
}

}  // namespace

ParseResult<DecodedTexture> DecodePng(
    std::span<const std::uint8_t> bytes,
    std::unique_ptr<std::vector<std::uint8_t>>& owned_pixels) {
  // Wuffs requires zero-initialized decoder structs.
  wuffs_png__decoder dec;
  std::memset(&dec, 0, sizeof(dec));
  wuffs_base__status init_status = wuffs_png__decoder__initialize(
      &dec, sizeof__wuffs_png__decoder(), WUFFS_VERSION, 0);
  if (init_status.repr != nullptr) {
    return std::unexpected(StatusToError(&init_status));
  }

  wuffs_base__io_buffer src = wuffs_base__ptr_u8__reader(
      const_cast<std::uint8_t*>(bytes.data()), bytes.size(), /*closed=*/true);

  wuffs_base__image_config img_cfg;
  std::memset(&img_cfg, 0, sizeof(img_cfg));
  wuffs_base__status cfg_status =
      wuffs_base__image_decoder__decode_image_config(
          wuffs_png__decoder__upcast_as__wuffs_base__image_decoder(&dec),
          &img_cfg,
          &src);
  if (cfg_status.repr != nullptr) {
    return std::unexpected(StatusToError(&cfg_status));
  }
  if (!wuffs_base__image_config__is_valid(&img_cfg)) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kTextureDecodeFailed,
                       std::string{"invalid image_config"},
                       0));
  }

  const std::uint32_t width = wuffs_base__pixel_config__width(&img_cfg.pixcfg);
  const std::uint32_t height =
      wuffs_base__pixel_config__height(&img_cfg.pixcfg);
  if (width == 0 || height == 0) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kTextureDecodeFailed,
                       std::string{"zero-width or zero-height image"},
                       0));
  }

  // Force RGBA_NONPREMUL output so Wuffs converts from the PNG's native
  // format (RGB, palette, grayscale, ...) during decode.
  wuffs_base__pixel_config__set(&img_cfg.pixcfg,
                                WUFFS_BASE__PIXEL_FORMAT__RGBA_NONPREMUL,
                                WUFFS_BASE__PIXEL_SUBSAMPLING__NONE,
                                width,
                                height);

  const std::uint64_t pixel_bytes = static_cast<std::uint64_t>(width) *
                                    static_cast<std::uint64_t>(height) * 4ull;
  if (pixel_bytes > kMaxPixelBytes) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kTextureDecodeFailed,
                       std::string{"image exceeds 256 MiB pixel buffer cap"},
                       0));
  }
  if (!owned_pixels) {
    owned_pixels = std::make_unique<std::vector<std::uint8_t>>();
  }
  owned_pixels->resize(static_cast<std::size_t>(pixel_bytes));

  // The plane-based layout may use a stride != width * 4, so writes must
  // go through this wrapper rather than a raw span.
  wuffs_base__pixel_buffer pixbuf;
  std::memset(&pixbuf, 0, sizeof(pixbuf));
  wuffs_base__slice_u8 dst =
      wuffs_base__make_slice_u8(owned_pixels->data(), owned_pixels->size());
  wuffs_base__status set_status =
      wuffs_base__pixel_buffer__set_from_slice(&pixbuf, &img_cfg.pixcfg, dst);
  if (set_status.repr != nullptr) {
    return std::unexpected(StatusToError(&set_status));
  }

  // Scratch buffer for intermediate decode state (e.g. zlib), allocated
  // locally so cache hits do not pay for it.
  wuffs_base__range_ii_u64 workbuf_range =
      wuffs_base__image_decoder__workbuf_len(
          wuffs_png__decoder__upcast_as__wuffs_base__image_decoder(&dec));
  std::vector<std::uint8_t> workbuf_storage;
  if (workbuf_range.max_incl > 0) {
    workbuf_storage.resize(static_cast<std::size_t>(workbuf_range.max_incl));
  }
  wuffs_base__slice_u8 workbuf =
      wuffs_base__make_slice_u8(workbuf_storage.data(), workbuf_storage.size());

  // PNGs are single-frame, so exactly one decode_frame call. v0.4 dropped
  // the v0.3 `dirty_rect`/`bbox` params in favor of `opts`; nullptr uses
  // the defaults.
  wuffs_base__status frame_status = wuffs_base__image_decoder__decode_frame(
      wuffs_png__decoder__upcast_as__wuffs_base__image_decoder(&dec),
      &pixbuf,
      &src,
      WUFFS_BASE__PIXEL_BLEND__SRC,
      workbuf,
      /*opts=*/nullptr);
  if (frame_status.repr != nullptr) {
    return std::unexpected(StatusToError(&frame_status));
  }

  // For RGBA_NONPREMUL the layout should already be contiguous, but the
  // spec is unclear about plane strides; copy row by row when it is not.
  // A plane smaller than the image cannot be repacked without reading
  // out of bounds, so reject it as a decode failure.
  wuffs_base__table_u8 plane = wuffs_base__pixel_buffer__plane(&pixbuf, 0);
  if (plane.width < width * 4 || plane.height < height) {
    return std::unexpected(
        ParseError::At(ParseError::Code::kTextureDecodeFailed,
                       std::string{"pixel buffer plane smaller than image"},
                       0));
  }
  if (plane.stride != static_cast<std::size_t>(width) * 4) {
    std::uint8_t* dst_ptr = owned_pixels->data();
    for (std::uint32_t y = 0; y < height; ++y) {
      std::memcpy(
          dst_ptr + y * width * 4, plane.ptr + y * plane.stride, width * 4);
    }
  }

  return DecodedTexture{
      std::span<const std::uint8_t>(owned_pixels->data(), owned_pixels->size()),
      width,
      height,
  };
}

}  // namespace fschema::geometry::internal
