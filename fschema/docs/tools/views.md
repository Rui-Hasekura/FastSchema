# Views

`fschema/filters/view.h` defines `BasicView`, `MakeView`, `View`, and
`LinearIndex`. `fschema/filters/pos.h` defines `LocalPos`, `Axis`, `Cmp`,
and `LocalPosHash`.

A view pairs a `Region` with a filter. It walks the region's blocks,
invokes the filter on each one, and passes only the matches to the
callback. The view holds a pointer to the region; it does not own the
region and does not copy block data.

## LocalPos

```cpp
struct LocalPos {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;

  friend bool operator==(const LocalPos&, const LocalPos&) = default;
};
```


A block coordinate relative to a region's origin. For a position inside a region with bounds `B`, `0 <= x < B.size[0]`, and the same for `y` and `z`. Filters and editors receiving a `LocalPos` assume it is in bounds. The 
`filters::internal` neighbor helpers are the exception; they treat out-of-bounds coordinates as air.

`LocalPosHash` is provided for use with `absl::flat_hash_set` and 
`absl::flat_hash_map`.

## Axis and Cmp

```cpp
enum class Axis : std::uint8_t { kX = 0, kY = 1, kZ = 2 };

enum class Cmp : std::uint8_t {
  kLt, kLe, kGe, kGt, kEq, kNe,
};
```

`Axis` values match the array indices of `BoundingBox::size` and 
`BoundingBox::origin`. `Cmp` is the comparison operator used by spatial filters such as `AxisFilter`.

## LinearIndex

```cpp
[[nodiscard]] inline std::uint64_t LinearIndex(
    LocalPos p, const ir::BoundingBox& b) noexcept;
```

Maps a 3D `LocalPos` to a 1D subscript into `Region::block_indices`. The layout is YZX: Y major, Z middle, X minor.

```
idx = y * (size_x * size_z) + z * size_x + x
```

Use `LinearIndex` instead of writing the formula by hand.

## BasicView

```cpp
template <Filter F>
class BasicView;
```

| Member              | Description                                                                                                                                              |
| ------------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `ForEach(fn)`       | Visits selected blocks as `fn(LocalPos, palette_idx)`.                                                                                                   |
| `ForEachLinear(fn)` | Visits selected blocks as `fn(linear_idx, palette_idx)`. Use this when writing only to `block_indices[linear_idx]`; it avoids reconstructing `LocalPos`. |
| `Count()`           | Returns the number of selected blocks. O(volume).                                                                                                        |
| `region()`          | Returns `ir::Region&`, even from a const view.                                                                                                           |
| `filter() const&`   | Returns `const F&`.                                                                                                                                      |
| `filter() &&`       | Returns `F&&`.                                                                                                                                           |

```cpp
const auto& bounds = region.bounds;
const std::int32_t sx = bounds.size[0];
const std::int32_t sy = bounds.size[1];
const std::int32_t sz = bounds.size[2];

for (std::int32_t y = 0; y < sy; ++y) {
  for (std::int32_t z = 0; z < sz; ++z) {
    for (std::int32_t x = 0; x < sx; ++x) {
      const LocalPos p{x, y, z};
      const std::uint16_t pal = region.block_indices[LinearIndex(p, bounds)];
      if (filter_(p, pal)) {
        fn(p, pal);
      }
    }
  }
}
```

`BasicView<G>` can be converted to `BasicView<F>` when `F` is constructible from `G`. This is used for type erasure.

## View

```cpp
using View = BasicView<std::function<bool(LocalPos, std::uint16_t)>>;
```

Use `View` when the filter type must be erased, for example when storing views of different filters in one container. `MakeView` returns a `BasicView<F>`; the converting constructor handles the erasure.

## MakeView

```cpp
template <Filter F>
[[nodiscard]] ParseResult<BasicView<F>> MakeView(ir::Region& r,
                                                 F f,
                                                 memory::Arena& arena);
```

Constructs a view. If `r.is_materialized` is false, `MakeView` calls 
`EnsureMaterialized` and propagates a `ParseError` on failure.

Direct construction of `BasicView` assumes `r.is_materialized == true`.

```cpp
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}
const fl::BasicView<fl::BoxFilter>& view = *view_res;

view.ForEach([](fl::LocalPos p, std::uint16_t palette_idx) {
  // Called once per selected block.
});
```

The view does not own the region. The region must outlive the view.

## Filter

`Filter` is defined in `fschema/filters/concepts.h`:

```cpp
template <class F>
concept Filter =
    std::regular_invocable<const F&, LocalPos, std::uint16_t> &&
    std::convertible_to<
        std::invoke_result_t<const F&, LocalPos, std::uint16_t>,
        bool>;
```

A type satisfies `Filter` if `const F&` is callable with a `LocalPos` and a palette index, and the result is convertible to `bool`. Filters must be const-callable because `BasicView::ForEach` passes the filter by const reference.

See [Filters](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters.md) for the built-in filters and composition.
