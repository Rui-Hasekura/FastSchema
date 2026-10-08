# AxisFilter

`fschema/filters/axis_filter.h` defines `AxisFilter` and the `WorldAxis` factory.

```cpp
struct AxisFilter {
  Axis axis;
  Cmp op;
  std::int32_t threshold;

  [[nodiscard]] constexpr bool operator()(LocalPos p,
                                          std::uint16_t /*idx=*/) const noexcept {
    const std::int32_t v = (axis == Axis::X)   ? p.x
                           : (axis == Axis::Y) ? p.y
                                               : p.z;
    switch (op) {
      case Cmp::Lt: return v < threshold;
      case Cmp::Le: return v <= threshold;
      case Cmp::Ge: return v >= threshold;
      case Cmp::Gt: return v > threshold;
      case Cmp::Eq: return v == threshold;
      case Cmp::Ne: return v != threshold;
    }
    return false;
  }
};
```

Selects blocks whose local coordinate on `axis` satisfies `axis_value op threshold`.

`Axis` and `Cmp` are defined in `pos.h`; see `pos.md`.

The palette index is ignored.

## WorldAxis

```cpp
[[nodiscard]] inline AxisFilter WorldAxis(const ir::Region& r,
                                          Axis axis,
                                          Cmp op,
                                          std::int32_t world_threshold) noexcept;
```

Converts a world-space threshold to a local threshold by subtracting `r.bounds.origin[static_cast<int>(axis)]`.

## Example

```cpp
namespace fl = fschema::filters;

// Select all blocks with local Y >= 5.
fl::AxisFilter y_high{fl::Axis::Y, fl::Cmp::Ge, 5};

// Same condition, expressed as a world-space Y threshold.
auto world_y = fl::WorldAxis(region, fl::Axis::Y, fl::Cmp::Ge, 69);

auto view_res = fl::MakeView(region, y_high, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});
```
