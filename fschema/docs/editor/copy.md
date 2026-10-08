# Copy & Move

`fschema/editors/copy.h` and `fschema/editors/move.h` define `Copy` and `Move`.

`Copy` writes selected blocks into a target region. `Move` shifts selected blocks within a single region.

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

Copies selected blocks from `v`'s source region to `target`, at `target_pos + (dx, dy, dz)`.

The offset is applied in local coordinates. A block at local `(x, y, z)` in the source lands at local `(x + dx, y + dy, z + dz)` in the target.

Out-of-bounds destinations in the target are silently discarded. A block whose destination falls outside the target's bounds is skipped; the source is not modified.

Palette entries are remapped by name through `ResolveOrAppend`. Only the name is carried over; properties are not. Two source entries with the same name but different properties collapse to the same target index.

The target region must be materialized. `Copy` calls `EnsureMaterialized(target, arena)` and propagates the error if materialization fails.

```cpp
namespace fl = fschema::filters;
namespace ed = fschema::editors;

fl::BoxFilter box{0, 0, 0, 9, 9, 9};
auto view_res = fl::MakeView(source_region, box, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}

// Copy the selection to target_region, shifted by (10, 0, 0).
ed::Copy(*view_res, target_region, 10, 0, 0, arena);
```

`Copy` calls `MarkEdited` on `target`, not on the source.

## Move

Sets selected blocks in the source to air, and writes their original palette indices to the shifted positions.

Out-of-bounds destinations are discarded: the block vanishes, but the source is still cleared. There is no rollback for partially out-of-bounds moves.

Overlapping moves are handled correctly. `Move` collects all selected blocks into a `std::vector<Entry>` before writing anything, so a block that is both a source and a destination sees the original value, not an intermediate one. The implementation then writes air to every source position in one pass, and the destination values in a second pass.

```cpp
// Move the selection up by 1 block.
ed::Move(*view_res, 0, 1, 0);
```

The source and destination are the same region. `Move` calls `MarkEdited` on it.
