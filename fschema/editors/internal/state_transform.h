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

#ifndef FSCHEMA_EDITORS_INTERNAL_STATE_TRANSFORM_H_
#define FSCHEMA_EDITORS_INTERNAL_STATE_TRANSFORM_H_

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <string_view>

#include "fschema/base/limits.h"
#include "fschema/base/nbt/reader.h"
#include "fschema/base/nbt/skip.h"
#include "fschema/base/nbt/tag.h"
#include "fschema/base/nbt/writer.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::editors {

// Controls whether Mirror / Rotate reorients directional block states
// alongside coordinate changes.
//
// - kGeometricOnly (default): Only updates positions; faster.
// - kTransformStates: Re-encodes block properties to match transformed
// geometry,
//   requiring an extra pass over the palette.
enum class StateTransformMode : std::uint8_t {
  kGeometricOnly = 0,
  kTransformStates = 1,
};

}  // namespace fschema::editors

namespace fschema::editors::internal {

// Which transform to apply to a direction vector.
enum class AxisOpType : std::uint8_t {
  kMirror,
  kRotate,
};

struct Vec3i {
  std::int32_t x;
  std::int32_t y;
  std::int32_t z;
};

// Negates the component aligned with `axis`.
// Mirrors the coordinate swap performed by editors::Mirror.
[[nodiscard]] inline Vec3i ApplyMirrorVec(Vec3i v,
                                          filters::Axis axis) noexcept {
  switch (axis) {
    case filters::Axis::X:
      return {-v.x, v.y, v.z};
    case filters::Axis::Y:
      return {v.x, -v.y, v.z};
    case filters::Axis::Z:
      return {v.x, v.y, -v.z};
  }
  return v;
}

// Rotates a direction vector by `steps` * 90 degrees clockwise around
// `axis`.
//
// The mapping must match editors::Rotate's coordinate transform exactly,
// otherwise facing values will be incorrect after the swap:
//   X: (dx, dy, dz) -> (dx,  dz, -dy)
//   Y: (dx, dy, dz) -> (-dz, dy,  dx)
//   Z: (dx, dy, dz) -> (dy, -dx,  dz)
[[nodiscard]] inline Vec3i ApplyRotateVec(Vec3i v,
                                          filters::Axis axis,
                                          std::int32_t steps) noexcept {
  steps = ((steps % 4) + 4) % 4;
  for (std::int32_t i = 0; i < steps; ++i) {
    Vec3i n = v;
    switch (axis) {
      case filters::Axis::X:
        n = {v.x, v.z, -v.y};
        break;
      case filters::Axis::Y:
        n = {-v.z, v.y, v.x};
        break;
      case filters::Axis::Z:
        n = {v.y, -v.x, v.z};
        break;
    }
    v = n;
  }
  return v;
}

// String <-> direction conversions for Minecraft's `facing` and `axis`
// property values. Unrecognized inputs map to the zero vector, which the
// caller treats as "leave this value alone".

[[nodiscard]] inline Vec3i FacingToVec(std::string_view s) noexcept {
  if (s == "north") return {0, 0, -1};
  if (s == "south") return {0, 0, 1};
  if (s == "east") return {1, 0, 0};
  if (s == "west") return {-1, 0, 0};
  if (s == "up") return {0, 1, 0};
  if (s == "down") return {0, -1, 0};
  return {0, 0, 0};
}

[[nodiscard]] inline std::string_view VecToFacing(Vec3i v) noexcept {
  // clang-format off
  if (v.x == 1  && v.y == 0  && v.z == 0)  return "east";
  if (v.x == -1 && v.y == 0  && v.z == 0)  return "west";
  if (v.x == 0  && v.y == 1  && v.z == 0)  return "up";
  if (v.x == 0  && v.y == -1 && v.z == 0)  return "down";
  if (v.x == 0  && v.y == 0  && v.z == 1)  return "south";
  if (v.x == 0  && v.y == 0  && v.z == -1) return "north";
  return {};
  // clang-format on
}

[[nodiscard]] inline Vec3i AxisToVec(std::string_view s) noexcept {
  if (s == "x") return {1, 0, 0};
  if (s == "y") return {0, 1, 0};
  if (s == "z") return {0, 0, 1};
  return {0, 0, 0};
}

[[nodiscard]] inline std::string_view VecToAxis(Vec3i v) noexcept {
  if (v.x != 0 && v.y == 0 && v.z == 0) return "x";
  if (v.x == 0 && v.y != 0 && v.z == 0) return "y";
  if (v.x == 0 && v.y == 0 && v.z != 0) return "z";
  return {};
}

// Applies a directional transform to a single (key, value) pair.
//
// Recognized keys: "facing" (6 directions), "axis" (x/y/z).
// Returns true and writes the new value to *out if the value actually changed;
// returns false for unrecognized keys/values or for values that the
// transform leaves unchanged.
[[nodiscard]] inline bool TransformPropValue(std::string_view key,
                                             std::string_view value,
                                             AxisOpType type,
                                             filters::Axis axis,
                                             std::int32_t steps,
                                             std::string* out) {
  const bool is_facing = (key == "facing");
  const bool is_axis = (key == "axis");
  if (!is_facing && !is_axis) return false;

  const Vec3i v = is_facing ? FacingToVec(value) : AxisToVec(value);
  if (v.x == 0 && v.y == 0 && v.z == 0) return false;

  const Vec3i n = (type == AxisOpType::kMirror)
                      ? ApplyMirrorVec(v, axis)
                      : ApplyRotateVec(v, axis, steps);

  if (n.x == v.x && n.y == v.y && n.z == v.z) return false;

  const std::string_view sv = is_facing ? VecToFacing(n) : VecToAxis(n);
  if (sv.empty()) return false;
  out->assign(sv);
  return true;
}

// Rewrites a "k1=v1,k2=v2" property string, transforming each pair.
//
// Returns the original span unchanged if no value changed,
// otherwise arena-owned bytes holding the rewritten string.
[[nodiscard]] inline std::span<const std::byte> TransformStringProps(
    std::span<const std::byte> props,
    AxisOpType type,
    filters::Axis axis,
    std::int32_t steps,
    memory::Arena& arena) {
  const std::string_view s(reinterpret_cast<const char*>(props.data()),
                           props.size());
  std::string result;
  result.reserve(s.size() + 16);

  bool changed = false;
  std::size_t pos = 0;
  while (pos < s.size()) {
    const auto comma = s.find(',', pos);
    const auto end = (comma == std::string_view::npos) ? s.size() : comma;
    const auto pair = s.substr(pos, end - pos);
    pos = end + 1;

    if (pair.empty()) continue;

    const auto eq = pair.find('=');
    if (eq != std::string_view::npos) {
      const auto key = pair.substr(0, eq);
      const auto val = pair.substr(eq + 1);
      std::string new_val;
      if (TransformPropValue(key, val, type, axis, steps, &new_val)) {
        changed = true;
        if (!result.empty()) result += ',';
        result.append(key);
        result += '=';
        result.append(new_val);
        continue;
      }
    }
    if (!result.empty()) result += ',';
    result.append(pair);
  }

  if (!changed) return props;

  void* mem = arena.Allocate(result.size());
  std::memcpy(mem, result.data(), result.size());
  return std::span<const std::byte>(static_cast<const std::byte*>(mem),
                                    result.size());
}

// Rewrites an NBT Compound body (litematica Properties),
// transforming String-tag values. Non-String fields are copied verbatim.
//
// Returns the original span unchanged if no value changed, otherwise
// arena-owned bytes holding the re-serialized compound
// (including its trailing End tag).
[[nodiscard]] inline std::span<const std::byte> TransformNbtProps(
    std::span<const std::byte> props,
    AxisOpType type,
    filters::Axis axis,
    std::int32_t steps,
    memory::Arena& arena) {
  base::NbtWriter writer(props.size() + 64);
  base::ByteReader reader(props, base::DecodeLimits{});

  bool changed = false;
  for (;;) {
    const std::size_t field_start = reader.pos();
    std::string_view name;
    auto tag = reader.ReadCompoundEntryHeaderView(name);
    if (!tag) return props;  // malformed input, keep original
    if (*tag == base::TagType::End) break;

    if (*tag == base::TagType::String) {
      auto val = reader.ReadStringView();
      if (!val) return props;
      std::string new_val;
      if (TransformPropValue(name, *val, type, axis, steps, &new_val)) {
        writer.WriteStringField(name, new_val);
        changed = true;
      } else {
        writer.WriteStringField(name, *val);
      }
    } else {
      auto skip = base::SkipPayload(reader, *tag);
      if (!skip) return props;
      // Copy the field verbatim (header + payload).
      writer.WriteListElementRawPayload(reader.SpanFrom(field_start));
    }
  }

  if (!changed) return props;

  writer.WriteEndTag();
  auto bytes = std::move(writer).Finalize();
  if (bytes.empty()) return props;

  void* mem = arena.Allocate(bytes.size());
  std::memcpy(mem, bytes.data(), bytes.size());
  return std::span<const std::byte>(static_cast<const std::byte*>(mem),
                                    bytes.size());
}

// Applies a directional transform to every palette entry in `r`. Entries
// whose properties did not change keep their original spans.
inline void TransformPalette(ir::Region& r,
                             AxisOpType type,
                             filters::Axis axis,
                             std::int32_t steps,
                             memory::Arena& arena) {
  for (auto& bs : r.palette) {
    if (bs.raw_properties.empty()) continue;
    if (bs.prop_encoding == ir::PropertyEncoding::kString) {
      bs.raw_properties =
          TransformStringProps(bs.raw_properties, type, axis, steps, arena);
    } else if (bs.prop_encoding == ir::PropertyEncoding::kNbt) {
      bs.raw_properties =
          TransformNbtProps(bs.raw_properties, type, axis, steps, arena);
    }
  }
}

}  // namespace fschema::editors::internal

#endif  // FSCHEMA_EDITORS_INTERNAL_STATE_TRANSFORM_H_