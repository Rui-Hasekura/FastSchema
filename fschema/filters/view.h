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

#ifndef FSCHEMA_FILTERS_VIEW_H_
#define FSCHEMA_FILTERS_VIEW_H_

#include <cstdint>
#include <functional>
#include <utility>

#include "fschema/base/error.h"
#include "fschema/base/port.h"
#include "fschema/filters/concepts.h"
#include "fschema/filters/pos.h"
#include "fschema/ir/materialize.h"
#include "fschema/ir/types.h"
#include "fschema/memory/arena.h"

namespace fschema::filters {

/// Converts a 3D `LocalPos` to a 1D linear index for `Region::block_indices`.
///
/// Memory Layout Invariant: YZX order (Y major, Z middle, X minor).
///   idx = y * (size_x * size_z) + z * size_x + x
[[nodiscard]] inline std::uint64_t LinearIndex(
    LocalPos p,
    const ir::BoundingBox& b) noexcept {
  const std::uint64_t sx = static_cast<std::uint64_t>(b.size[0]);
  const std::uint64_t sz = static_cast<std::uint64_t>(b.size[2]);
  return static_cast<std::uint64_t>(p.y) * (sx * sz) +
         static_cast<std::uint64_t>(p.z) * sx + static_cast<std::uint64_t>(p.x);
}

/// A lazy, non-owning projection of a `Region` through a `Filter`.
/// Region must outlive View.
///
/// Preconditions:
/// 1. The referenced `Region` must outlive the View.
/// 2. `r.is_materialized == true` must hold before construction, or `MakeView`
///    must be used to guarantee materialization.
template <Filter F>
class BasicView {
 public:
  /// Constructs a view. Assumes `r.is_materialized == true`.
  BasicView(ir::Region& r, F filter) noexcept
      : region_(&r), filter_(std::move(filter)) {}

  // Default copy/move (F must be copyable/movable).
  BasicView(BasicView&&) noexcept = default;
  BasicView(const BasicView&) = default;
  BasicView& operator=(BasicView&&) noexcept = default;
  BasicView& operator=(const BasicView&) = default;

  /// Converting constructor: `BasicView<G>` -> `BasicView<F>`.
  /// Used to convert a templated view to a type-erased `View`
  /// (`BasicView<std::function<...>>`).
  template <Filter G>
    requires std::is_constructible_v<F, const G&>
  BasicView(const BasicView<G>& other) noexcept(
      std::is_nothrow_constructible_v<F, const G&>)
      : region_(&other.region()), filter_(other.filter()) {}

  template <Filter G>
    requires std::is_constructible_v<F, G&&>
  BasicView(BasicView<G>&& other) noexcept(
      std::is_nothrow_constructible_v<F, G&&>)
      : region_(&other.region()), filter_(std::move(other).filter()) {}

  template <class Fn>
  void for_each(Fn&& fn) const {
    const auto& bounds = region_->bounds;
    const std::int32_t sx = bounds.size[0];
    const std::int32_t sy = bounds.size[1];
    const std::int32_t sz = bounds.size[2];
    const std::uint16_t* FSCHEMA_RESTRICT data = region_->block_indices.data();

    for (std::int32_t y = 0; y < sy; ++y) {
      const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
      for (std::int32_t z = 0; z < sz; ++z) {
        const std::uint64_t z_base =
            y_base + static_cast<std::uint64_t>(z) * sx;
        for (std::int32_t x = 0; x < sx; ++x) {
          const std::uint64_t li = z_base + x;
          const std::uint16_t pal = data[li];
          const LocalPos p{x, y, z};
          if (filter_(p, pal)) {
            fn(p, pal);
          }
        }
      }
    }
  }

  /// Iterates over selected blocks in YZX order, invoking
  /// `fn(linear_idx, palette_idx)`.
  ///
  /// Use this when you only need to write to `block_indices[linear_idx]`.
  /// It avoids the overhead of reconstructing `LocalPos` and recomputing
  /// `LinearIndex` inside the callback.
  template <class Fn>
  void for_each_linear(Fn&& fn) const {
    const auto& bounds = region_->bounds;
    const std::int32_t sx = bounds.size[0];
    const std::int32_t sy = bounds.size[1];
    const std::int32_t sz = bounds.size[2];
    const std::uint16_t* FSCHEMA_RESTRICT data = region_->block_indices.data();

    for (std::int32_t y = 0; y < sy; ++y) {
      const std::uint64_t y_base = static_cast<std::uint64_t>(y) * sx * sz;
      for (std::int32_t z = 0; z < sz; ++z) {
        const std::uint64_t z_base =
            y_base + static_cast<std::uint64_t>(z) * sx;
        for (std::int32_t x = 0; x < sx; ++x) {
          const std::uint64_t li = z_base + x;
          const std::uint16_t pal = data[li];
          const LocalPos p{x, y, z};
          if (filter_(p, pal)) {
            fn(li, pal);
          }
        }
      }
    }
  }

  /// Counts the number of selected blocks. O(volume) time.
  [[nodiscard]] std::size_t count() const {
    std::size_t n = 0;
    for_each([&n](LocalPos, std::uint16_t) { ++n; });
    return n;
  }

  /// Returns non-const ref even from a const View.
  [[nodiscard]] ir::Region& region() const noexcept { return *region_; }
  [[nodiscard]] const F& filter() const& noexcept { return filter_; }
  [[nodiscard]] F&& filter() && noexcept { return std::move(filter_); }

 private:
  ir::Region* region_;  // non-owning
  F filter_;
};

/// Type-erased View alias for storage when the filter type `F` is
/// unknown at compile time (e.g., in runtime polymorphic contexts).
using View = BasicView<std::function<bool(LocalPos, std::uint16_t)>>;

/// Factory function to safely construct a View.
/// Triggers `EnsureMaterialized()` on demand if the region is not yet decoded.
///
/// Returns `ParseError` if materialization fails (e.g., invalid disk data).
template <Filter F>
[[nodiscard]] ParseResult<BasicView<F>> MakeView(ir::Region& r,
                                                 F f,
                                                 memory::Arena& arena) {
  if (!r.is_materialized) {
    auto res = ir::EnsureMaterialized(r, arena);
    if (!res) return std::unexpected(res.error());
  }
  return BasicView<F>(r, std::move(f));
}

}  // namespace fschema::filters

#endif  // FSCHEMA_FILTERS_VIEW_H_