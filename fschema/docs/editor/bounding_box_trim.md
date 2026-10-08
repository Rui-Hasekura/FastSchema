# BoundingBoxTrim

`fschema/editors/bounding_box_trim.h` defines `BoundingBoxTrim`.

```cpp
[[nodiscard]] inline ParseResult<ir::Region> BoundingBoxTrim(
    ir::Region& r,
    memory::Arena& arena);
```

Scans `r` for non-air blocks, computes the tightest local bounding box containing them, and returns a new compact region covering exactly that box.

The input region is only read; its block data is not modified. `EnsureMaterialized(r, arena)` is called first, so a lazy region gets materialized as a side effect, but `edited` and `palette_pristine` are not changed.

## Air test

A block is treated as non-air if its palette index is in range and its name is not one of `minecraft:air`, `minecraft:cave_air`, or `minecraft:void_air`.

## Empty input

If the region contains only air, `BoundingBoxTrim` returns an empty region:

```
name           = "trimmed_empty"
bounds.origin  = {0, 0, 0}
bounds.size    = {0, 0, 0}
position       = {0, 0, 0}
size           = {0, 0, 0}
is_materialized = true
```

## Implementation note

The scan produces local min/max coordinates. Those are wrapped in a `BoxFilter` and passed through `Extract`, which is where palette subsetting and buffer allocation happen. See `extract.md` for the resulting region's invariants.

## Example

```cpp
namespace ed = fschema::editors;

auto trimmed = ed::BoundingBoxTrim(region, arena);
if (!trimmed) {
  // Handle ParseError.
  return;
}
if (trimmed->bounds.size[0] == 0) {
  // The input region was all air.
}

// trimmed is a new compact region. The original `region` is unchanged
// apart from being materialized.
```
