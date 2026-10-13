# Spatial Filters

`fschema/filters/box_filter.h`, `sphere_filter.h`, and `axis_filter.h`
define filters that decide from `LocalPos` alone. The palette index is
ignored.

## BoxFilter

```cpp
struct BoxFilter {
  std::int32_t min_x, min_y, min_z;
  std::int32_t max_x, max_y, max_z;  // inclusive
};
```

Selects blocks inside an axis-aligned box in local coordinates. Bounds are inclusive on every axis.

```cpp
[[nodiscard]] inline BoxFilter WorldBox(const ir::Region& r,
                                        std::int32_t wmin_x,
                                        std::int32_t wmin_y,
                                        std::int32_t wmin_z,
                                        std::int32_t wmax_x,
                                        std::int32_t wmax_y,
                                        std::int32_t wmax_z) noexcept;
```

`WorldBox` subtracts `r.bounds.origin` from each bound and returns a 
`BoxFilter`.

```cpp
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

view_res->ForEach([](fl::LocalPos p, std::uint16_t /*pal*/) {
  // p.x in [0, 9], p.y in [0, 9], p.z in [0, 9]
});
```

## SphereFilter

```cpp
struct SphereFilter {
  std::int32_t cx, cy, cz;
  std::int64_t r2;
};
```

Selects blocks whose squared distance to `(cx, cy, cz)` is `<= r2`. Pass `radius * radius`, not `radius`. The palette index is ignored. Points on the surface are included.

```cpp
fl::SphereFilter sphere{5, 5, 5, 3 * 3};
```

## AxisFilter

```cpp
struct AxisFilter {
  Axis axis;
  Cmp op;
  std::int32_t threshold;
};
```

Selects blocks whose local coordinate on `axis` satisfies `axis_value op threshold`. `Axis` and `Cmp` are defined in `fschema/filters/pos.h`.

```cpp
[[nodiscard]] inline AxisFilter WorldAxis(
    const ir::Region& r,
    Axis axis,
    Cmp op,
    std::int32_t world_threshold) noexcept;
```

`WorldAxis` converts a world-space threshold to a local threshold by subtracting `r.bounds.origin[static_cast<int>(axis)]`.

```cpp
// Local Y >= 5.
fl::AxisFilter y_high{fl::Axis::kY, fl::Cmp::kGe, 5};

// Same condition expressed as a world-space threshold.
auto world_y = fl::WorldAxis(region, fl::Axis::kY, fl::Cmp::kGe, 69);
```

## Combining

All three are plain types and compose with `&&`, `||`, and `!`:

```cpp
fl::BoxFilter box{0, 0, 0, 9, 9, 9};
fl::SphereFilter sphere{5, 5, 5, 3 * 3};

// Inside the box and inside the sphere.
auto selection = box && sphere;
```
