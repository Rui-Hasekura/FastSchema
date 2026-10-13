# Editors

Editors mutate a region. Most take a `BasicView<F>` to select blocks and
write directly to `Region::block_indices`. Some return a new region
instead. All editor headers live under `fschema/editors/`.

## Common contract

Any editor that writes to `block_indices` must call
`fschema::editors::MarkEdited(r)` before returning. `MarkEdited` sets
`edited = true` and clears `lazy_source.palette_pristine`. If neither flag
is cleared, the encoder may take the lazy passthrough path and re-emit the
original packed bytes, discarding the edit.

`ResolveOrAppend` and `ResolveAir` also clear `palette_pristine` when they
append a palette entry. That alone is not a substitute for `MarkEdited`;
call it regardless.

Editors that do not touch `block_indices` do not call `MarkEdited`:

- Editors that return a new region and only read the input: `Extract`,
  `BoundingBoxTrim`, `Mirror`, `Rotate`.
- Editors that only rewrite `block_entities`: `EraseNbt`. The encoder
  reads `block_entities` at serialization time.
- Editors that only rewrite the palette: `ReplacePaletteEntry`. The block
  indices are unchanged, so the packed data is still valid.

## Return type

Most editors return `ParseResult<void>`. The result is `std::unexpected` if
materialization, palette resolution, or NBT rewriting fails.

Editors that produce a new region return `ParseResult<ir::Region>`.

`Overlay` returns `ParseResult<std::size_t>`, the number of blocks placed.

## Materialization

`MakeView` calls `EnsureMaterialized` if needed. Editors that call
`EnsureMaterialized` directly on a region do so before touching
`block_indices`; `Hollow` and `Overlay` are examples. `Copy` materializes
the target region only.

## Pages

- [Fill and replace](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/fill_replace.md):
  `Fill`, `Delete`, `Replace`, `ReplaceWith`.
- [Copy and move](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/copy_move.md):
  `Copy`, `Move`.
- [Extract and trim](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/extract_trim.md):
  `Extract`, `ComputeSelectionBounds`, `BoundingBoxTrim`.
- [Mirror and rotate](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/mirror_rotate.md):
  `Mirror`, `Rotate`, `StateTransformMode`.
- [Hollow](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/hollow.md)
- [Overlay](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/overlay.md)
- [EraseNbt](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/erase_nbt.md)
- [Palette utilities](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/palette_utils.md):
  `FindPaletteIndex`, `FindAirIndex`, `AppendPaletteEntry`,
  `ResolveOrAppend`, `ResolveAir`, `MarkEdited`, `ReplacePaletteEntry`.

See
[Custom Tools](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/custom_tools.md)
for writing a custom editor.
