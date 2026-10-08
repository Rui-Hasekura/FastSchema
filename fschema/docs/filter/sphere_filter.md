# SphereFilter

`fschema/filters/sphere_filter.h` defines `SphereFilter`.

```cpp
struct SphereFilter {
  std::int32_t cx, cy, cz;
  std::int64_t r2;

  [[nodiscard]] constexpr bool operator()(LocalPos p,
                                          std::uint16_t /*idx=*/) const noexcept {
    const std::int64_t dx = static_cast<std::int64_t>(p.x) - cx;
    const std::int64_t dy = static_cast<std::int64_t>(p.y) - cy;
    const std::int64_t dz = static_cast<std::int64_t>(p.z) - cz;
    return dx * dx + dy * dy + dz * dz <= r2;
  }
};
```

Selects blocks within a sphere centered at local `(cx, cy, cz)`.

`r2` is the squared radius. Store `radius * radius`, not `radius`; the comparison uses `r2` directly and avoids a square root in the inner loop.

The sphere includes points on the surface (`<=`).

The palette index is ignored.

## Example

```cpp
namespace fl = fschema::filters;

// Sphere centered at local (5, 5, 5) with radius 3.
fl::SphereFilter sphere{5, 5, 5, 3 * 3};

auto view_res = fl::MakeView(region, sphere, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});
```
