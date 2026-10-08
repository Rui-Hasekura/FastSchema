# Extract

`fschema/editors/extract.h` defines `ComputeSelectionBounds` and `Extract`.

Both take a view and do not modify the source region. `Extract` returns a new compact region.

```cpp
template <filters::Filter F>
[[nodiscard]] std::optional<ir::BoundingBox> ComputeSelectionBounds(
    const filters::BasicView<F>& v);

template <filters::Filter F>
[[nodiscard]] ParseResult<ir::Region> Extract(const filters::BasicView<F>& v,
                                              memory::Arena& arena);
```

## ComputeSelectionBounds

Scans the view and returns the bounding box of the selected blocks in world space.

Returns `std::nullopt` if no blocks are selected.

The returned box is in world space: the region's origin offset is added back. If the source region's origin is `(100, 64, -200)` and the selected blocks span local `(5, 0, 3)` to `(15, 10, 8)`, the result has origin `(105, 64, -197)` and size `(11, 11, 6)`.

```cpp
auto bounds = fschema::editors::ComputeSelectionBounds(*view_res);
if (bounds) {
  // bounds->origin, bounds->size
}
```

## Extract

Deep-copies the selected blocks into a new region whose bounds are exactly the world-space selection bounds.

The new palette is the subset of the source palette that is used by the selection, in the original order. Palette indices are remapped accordingly.

The output buffer is filled with the first palette entry before the second pass writes the selected blocks. Cells inside the new bounds that the selection did not cover keep that first entry. The first palette entry may or may not be air; the encoder normalizes air at serialization time.

If the selection is empty, the result is an empty region:

```
name             = "extract"
bounds.origin   = {0, 0, 0}
bounds.size     = {0, 0, 0}
position        = {0, 0, 0}
size            = {0, 0, 0}
is_materialized = true
```

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}

auto extracted = ed::Extract(*view_res, arena);
if (!extracted) {
  // Handle ParseError.
  return;
}
// extracted->bounds, extracted->palette, extracted->block_indices
```

## No side effects on the source

`Extract` only reads the source region. It does not call `MarkEdited`, does not touch `palette_pristine`, and does not modify the source's block data.

The output region is newly constructed with `is_materialized = true`. All allocation for the output buffer and palette goes through the supplied `memory::Arena`.

## bounds, position, and size

The returned region's `position` and `size` are set to `bounds.origin` and `bounds.size`. `bounds` is the authoritative field for iteration and filtering; `position` and `size` are the raw fields the encoder writes back for formats that carry them.
