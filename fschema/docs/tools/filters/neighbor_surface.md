# NeighborFilter and SurfaceFilter

`fschema/filters/neighbor_filter.h` and `surface_filter.h` define filters
that read `block_indices`. Both require a materialized region.

## NeighborFilter

```cpp
template <Filter F>
class NeighborFilter {
 public:
  NeighborFilter(const ir::Region& r, F f) noexcept;

  [[nodiscard]] bool operator()(LocalPos p,
                                std::uint16_t /*pal*/) const noexcept;
};
```

Selects blocks with at least one adjacent block (out of the six face neighbors) that matches the inner filter `F`. `F` is applied to the neighbor's `LocalPos` and palette index.

Neighbors outside the region bounds are not passed to `F`. A block on the border checks only the neighbors that exist.

`NeighborFilter` itself satisfies `Filter`.

```cpp
template <Filter F>
[[nodiscard]] NeighborFilter<F> MakeNeighborFilter(const ir::Region& r,
                                                   F f) noexcept;
```

Deduction helper. Prefer this over writing the template parameters manually.

```cpp
#include "fschema/filters/block_name_filter.h"
#include "fschema/filters/neighbor_filter.h"
#include "fschema/filters/view.h"

fl::IsAirFilter is_air{region};
auto exposed = fl::MakeNeighborFilter(region, is_air);

auto view_res = fl::MakeView(region, exposed, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

view_res->ForEach([](fl::LocalPos p, std::uint16_t pal) {
  // Every selected block touches air on at least one face.
});
```

## SurfaceFilter

```cpp
class SurfaceFilter {
 public:
  explicit SurfaceFilter(const ir::Region& r) noexcept;

  [[nodiscard]] bool operator()(LocalPos p,
                                std::uint16_t idx) const noexcept;
};
```

Selects non-air blocks that are exposed to air on at least one of their six faces. A block on the region border is exposed when the neighbor in that direction is out of bounds; out-of-bounds neighbors are treated as air. Air blocks are never selected.

The filter reads `block_indices` and `palette` on every candidate block. It does not precompute a LUT.

```cpp
fl::SurfaceFilter surface{region};
auto view_res = fl::MakeView(region, surface, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}
```

## Interior

The complement of `SurfaceFilter` restricted to non-air blocks is the interior:

```cpp
fl::SurfaceFilter surface{region};
fl::IsAirFilter is_air{region};
auto interior = !surface && !is_air;
```

`Hollow` uses this composition.

## Preconditions

Both filters read `block_indices`. Construct the view through `MakeView` so that the region is materialized before the filter is invoked. The region must outlive the filter.
