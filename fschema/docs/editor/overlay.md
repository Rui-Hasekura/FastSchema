# Overlay

`fschema/editors/overlay.h` defines `Overlay`.

```cpp
template <filters::Filter F>
[[nodiscard]] ParseResult<std::size_t> Overlay(filters::BasicView<F> v,
                                               std::string_view block_name,
                                               memory::Arena& arena);
```

Places `block_name` on top of every selected surface in the view.

A surface is a non-air block whose `Y+1` neighbor is air or out of bounds. If `Y+1` is outside the region, the block is treated as a surface and no placement happens, since the target position would also be out of bounds.

```cpp
v.for_each([&](filters::LocalPos p, std::uint16_t pal) {
  if (pal >= r.palette.size() || base::IsAirVariant(r.palette[pal].name)) {
    return;
  }

  const std::int32_t ty = p.y + 1;
  if (ty >= bounds.size[1]) {
    return;
  }

  const std::uint64_t top_linear = filters::LinearIndex({p.x, ty, p.z}, bounds);
  const std::uint16_t top_pal = r.block_indices[top_linear];

  if (top_pal >= r.palette.size() ||
      base::IsAirVariant(r.palette[top_pal].name)) {
    targets.push_back(top_linear);
  }
});
```

Target positions are collected into a `std::vector<std::uint64_t>` before any writes, so placing a block does not create new surfaces that would cascade within the same call.

The function returns the number of blocks placed. If no targets are found, it returns `0` without calling `MarkEdited`.

## Preconditions

The region is materialized via `EnsureMaterialized` at the start.

## MarkEdited

`Overlay` calls `MarkEdited(r)` only when at least one target was found.

## Example

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}

auto placed = ed::Overlay(*view_res, "minecraft:snow_block", arena);
if (!placed) {
  // Handle ParseError.
  return;
}
std::size_t count = *placed;  // number of blocks placed
```
