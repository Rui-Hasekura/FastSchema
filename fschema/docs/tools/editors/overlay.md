# Overlay

`fschema/editors/overlay.h` defines `Overlay`.

```cpp
template <filters::Filter F>
[[nodiscard]] ParseResult<std::size_t> Overlay(
    filters::BasicView<F> v,
    std::string_view block_name,
    memory::Arena& arena);
```

Places `block_name` on top of every selected surface in the view, and returns the number of blocks placed.

A surface is a non-air block whose `Y + 1` neighbor is air or out of bounds. If `Y + 1` is outside the region, the block counts as a surface but no placement happens, since the target position is also out of bounds.

## Target collection

Target positions are collected into a `std::vector<std::uint64_t>` before any writes. Placing a block during the scan would create a new surface at the next layer up, cascading within the same call.

The destination is always a single block: the cell at `p.y + 1`. A surface whose top cell is already occupied by a non-air block is skipped.

## Return value and MarkEdited

- Returns `0` when no targets are found, without calling `MarkEdited`.

- Returns the number of blocks placed otherwise. `MarkEdited(r)` is called in that case.

## Preconditions

The region is materialized through `EnsureMaterialized` at the start of the call.

## Example

```cpp
#include "fschema/editors/overlay.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(region, box, *schematic.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

auto placed = ed::Overlay(*view_res, "minecraft:snow_block", *schematic.arena);
if (!placed) {
  const fschema::ParseError& e = placed.error();
  return;
}
const std::size_t count = *placed;
```


