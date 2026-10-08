# View

`BasicView` is a lazy, non-owning projection of a `Region` through a
`Filter`. It does not own the `Region`; the referenced `Region` must
outlive the view.

Prefer `MakeView` over constructing `BasicView` directly.

## Filter

`Filter` is a C++20 concept defined in `fschema/filters/concepts.h`:

```cpp
template <class F>
concept Filter =
    std::regular_invocable<const F&, LocalPos, std::uint16_t> &&
    std::convertible_to<std::invoke_result_t<const F&, LocalPos, std::uint16_t>,
                        bool>;
```

A type `F` satisfies `Filter` if `const F&` is callable with a
`LocalPos` and a palette index, and the result is convertible to `bool`.

Filters may inspect the local position, the palette index, or both.
They must be const-callable because `BasicView::for_each` passes the
filter by const reference.

## LinearIndex

`LinearIndex` is defined in `fschema/filters/view.h`.

```cpp
[[nodiscard]] inline std::uint64_t LinearIndex(
    LocalPos p, const ir::BoundingBox& b) noexcept;
```

It maps a 3D `LocalPos` to a 1D subscript into `Region::block_indices`,
using YZX order:

`idx = y * (size_x * size_z) + z * size_x + x`

Use it instead of writing the formula by hand. See `pos.md` for the
layout invariant.

## BasicView

`BasicView` is defined in `fschema/filters/view.h`.

| Member                | Description                                                                                                                                                                                         |
| --------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| `for_each(fn)`        | Visits selected blocks in YZX order as `fn(LocalPos, palette_idx)`.                                                                                                                                 |
| `for_each_linear(fn)` | Visits selected blocks in YZX order as `fn(linear_idx, palette_idx)`. Use this when writing only to `block_indices[linear_idx]`; it avoids reconstructing `LocalPos` and recomputing `LinearIndex`. |
| `count()`             | Returns the number of selected blocks. O(volume).                                                                                                                                                   |
| `region()`            | Returns `ir::Region&`, even from a const view. The view does not own it.                                                                                                                            |
| `filter() const&`     | Returns `const F&`.                                                                                                                                                                                 |
| `filter() &&`         | Returns `F&&` and moves the filter out.                                                                                                                                                             |

`BasicView<G>` can be converted to `BasicView<F>` when `F` is
constructible from `G`. This is mainly for type erasure, such as
`BasicView<std::function<...>>`. Most users do not need to use this
directly.

## View

```cpp
using View = BasicView<std::function<bool(LocalPos, std::uint16_t)>>;
```

Use `View` when the filter type must be erased, for example when storing
views of different filters in one container. Construct it via `MakeView`;
the converting constructor of `BasicView` handles the type erasure.

## Construction

Use `MakeView` to construct a view safely:

```cpp
auto view_res = fl::MakeView(region, filter, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
const auto& view = *view_res;
```

`MakeView` calls `EnsureMaterialized()` only if `region.is_materialized`
is false. If materialization fails, it returns `ParseError`.

Direct construction of `BasicView` assumes
`region.is_materialized == true`.

## Example

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

// BoxFilter parameters: min_x, min_y, min_z, max_x, max_y, max_z.
fl::BoxFilter box{0, 0, 0, 9, 9, 9};

auto view_res = fl::MakeView(region, box, arena);
if (!view_res) {
  // Handle error.
  return;
}
const auto& view = *view_res;

view.for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  std::println("Selected at ({}, {}, {}), palette={}",
               p.x, p.y, p.z, palette_idx);
});

// Pass the view to an editor for modification.
ed::Fill(view, "minecraft:stone");
```
