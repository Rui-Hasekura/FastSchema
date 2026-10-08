# SurfaceFilter

`fschema/filters/surface_filter.h` defines `SurfaceFilter`.

```cpp
class SurfaceFilter {
 public:
  explicit SurfaceFilter(const ir::Region& r) noexcept;

  [[nodiscard]] bool operator()(LocalPos p, std::uint16_t idx) const noexcept;

 private:
  const ir::Region* region_;
};
```

Selects non-air blocks that are exposed to air on at least one of their six faces. A block on the region border is exposed if the neighbor in that direction is out of bounds; `filters::internal::IsAirAt` treats out-of-bounds as air.

Air blocks are never selected, regardless of their neighbors.

`SurfaceFilter` reads `region_->block_indices` and `region_->palette` for every candidate block; it does not precompute a LUT.

## Preconditions

`region.is_materialized == true` must hold before constructing the filter. `MakeView` guarantees this.

The referenced region must outlive the filter.

## Example

```cpp
namespace fl = fschema::filters;

fl::SurfaceFilter surface{region};

auto view_res = fl::MakeView(region, surface, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});

// Typical composite: solid blocks not on the surface, i.e. the interior.
fl::IsAirFilter is_air{region};
auto interior = !surface && !is_air;
```

`Hollow` uses exactly this composition; see `editors/hollow.h`.
