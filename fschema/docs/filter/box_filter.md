# BoxFilter

`fschema/filters/box_filter.h` defines `BoxFilter` and the `WorldBox` factory.

## BoxFilter

```cpp
struct BoxFilter {
  std::int32_t min_x, min_y, min_z;
  std::int32_t max_x, max_y, max_z;  // inclusive bounds

  [[nodiscard]] constexpr bool operator()(LocalPos p,
                                          std::uint16_t /*idx=*/) const noexcept {
    return p.x >= min_x && p.x <= max_x && p.y >= min_y && p.y <= max_y &&
           p.z >= min_z && p.z <= max_z;
  }
};
```

Selects blocks within an axis-aligned 3D bounding box. Bounds are inclusive. Coordinates are region-local.

The palette index is ignored.

## WorldBox

```cpp
[[nodiscard]] inline BoxFilter WorldBox(const ir::Region& r,
                                        std::int32_t wmin_x,
                                        std::int32_t wmin_y,
                                        std::int32_t wmin_z,
                                        std::int32_t wmax_x,
                                        std::int32_t wmax_y,
                                        std::int32_t wmax_z) noexcept;
```

Converts world-space coordinates to region-local by subtracting `r.bounds.origin`.

## Example

```cpp
namespace fl = fschema::filters;

// Select a 10x10x10 cuboid starting at local (0, 0, 0).
fl::BoxFilter box{0, 0, 0, 9, 9, 9};

auto view_res = fl::MakeView(region, box, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});

// From world-space coordinates:
auto world_box = fl::WorldBox(region, -5, 0, -5, 4, 9, 4);
```
