# Extract and BoundingBoxTrim

`fschema/editors/extract.h` defines `ComputeSelectionBounds` and
`Extract`. `fschema/editors/bounding_box_trim.h` defines
`BoundingBoxTrim`.
All three read the input region and do not modify its block data.
`Extract` and `BoundingBoxTrim` return a new compact `ir::Region`.

```cpp
template <filters::Filter F>
[[nodiscard]] std::optional<ir::BoundingBox> ComputeSelectionBounds(
    const filters::BasicView<F>& v);

template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Extract(
    const filters::BasicView<F>& v,
    memory::Arena& arena);

[[nodiscard]] inline ParseResult<ir::Region> BoundingBoxTrim(
    ir::Region& r,
    memory::Arena& arena);
```

## ComputeSelectionBounds

Scans the view and returns the world-space bounding box of the selected blocks. Returns `std::nullopt` if no blocks are selected.

The result is in world space: the region's origin is added back. For a region with origin `(100, 64, -200)` and selected blocks spanning local `(5, 0, 3)` to `(15, 10, 8)`, the result has origin `(105, 64, -197)` and size `(11, 11, 6)`.

## Extract

Deep-copies the selected blocks into a new region whose bounds are exactly the world-space selection bounds.

The new palette is the subset of the source palette used by the selection, in the source order. Indices are remapped accordingly. Cells inside the new bounds that the selection did not cover are filled with the first palette entry.

An empty selection returns an empty region:

```
name            = "extract"
bounds.origin   = {0, 0, 0}
bounds.size     = {0, 0, 0}
position        = {0, 0, 0}
size            = {0, 0, 0}
is_materialized = true
```

`Extract` does not call `MarkEdited`. It does not touch the source's 
`block_indices`, `palette_pristine`, or `edited`.

```cpp
#include "fschema/editors/extract.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, *schematic.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

auto extracted = ed::Extract(*view_res, *schematic.arena);
if (!extracted) {
  const fschema::ParseError& e = extracted.error();
  return;
}
// extracted->bounds, extracted->palette, extracted->block_indices
```

## BoundingBoxTrim

Scans `r` for non-air blocks and returns a new compact region covering exactly their local bounding box. `r` is only read; its block data is unchanged. `EnsureMaterialized` is called first, so a lazy region becomes materialized as a side effect.

A block is treated as non-air if its palette index is in range and its name is not one of `minecraft:air`, `minecraft:cave_air`, or 
`minecraft:void_air`.

If the region contains only air, `BoundingBoxTrim` returns the empty region:

```
name            = "trimmed_empty"
bounds.origin   = {0, 0, 0}
bounds.size     = {0, 0, 0}
position        = {0, 0, 0}
size            = {0, 0, 0}
is_materialized = true
```

```cpp
#include "fschema/editors/bounding_box_trim.h"

auto trimmed = ed::BoundingBoxTrim(region, *schematic.arena);
if (!trimmed) {
  const fschema::ParseError& e = trimmed.error();
  return;
}
if (trimmed->bounds.size[0] == 0) {
  // The input region was all air.
}
```

## bounds, position, and size

For the returned region, `bounds` is authoritative for iteration and filtering. `position` and `size` are the raw fields the encoder writes back for formats that carry them; both are set from `bounds`.


