# Copy and Move

`fschema/editors/copy.h` and `move.h` define `Copy` and `Move`.

```cpp
template <filters::Filter F>
[[nodiscard]] ParseResult<void> Copy(const filters::BasicView<F>& v,
                                     ir::Region& target,
                                     std::int32_t dx,
                                     std::int32_t dy,
                                     std::int32_t dz,
                                     memory::Arena& arena);

template <filters::Filter F>
[[nodiscard]] ParseResult<void> Move(filters::BasicView<F> v,
                                     std::int32_t dx,
                                     std::int32_t dy,
                                     std::int32_t dz);
```

## Copy

Copies selected blocks from the view's source region into `target`, at  `target_pos + (dx, dy, dz)`. The offset is local to each region.

A block whose destination falls outside the target's bounds is skipped. The source is not modified.

Palette entries are remapped by name through `ResolveOrAppend`. Only the name is carried over; properties are not. Two source entries with the same name and different properties collapse to the same target index.

`Copy` materializes `target` if needed and calls `MarkEdited` on `target`, not on the source.

```cpp
#include "fschema/editors/copy.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(source_region, box, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

auto res = ed::Copy(*view_res, target_region, /*dx=*/10, /*dy=*/0, /*dz=*/0,
                    *schema.arena);
if (!res) {
  const fschema::ParseError& e = res.error();
  return;
}
```

## Move

Clears every selected block to air and writes the original palette index to the shifted position. The source and the destination are the same region.

A block whose destination falls outside the region is discarded, but the source is still cleared. There is no rollback for partially out-of-bounds moves.

Overlapping moves are handled correctly: the intended writes are collected before any are applied, so a block that is both a source and a destination sees the original value, not an intermediate one.

`Move` calls `MarkEdited` on the region.

```cpp
auto res = ed::Move(*view_res, /*dx=*/0, /*dy=*/1, /*dz=*/0);
if (!res) {
  const fschema::ParseError& e = res.error();
  return;
}
```

A zero shift is a no-op and returns immediately.
