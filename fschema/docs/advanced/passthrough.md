# Passthrough Encoding

The encoder can skip decoding and re-encoding block indices when the
region has not been modified since it was decoded. This page describes
the condition and its two flags.

## Flags

Two fields on `Region` control the decision.

```cpp
mutable bool edited = false;              // set by MarkEdited
// lazy_source.palette_pristine           // cleared by palette helpers
```

`edited` is set to `true` by `editors::MarkEdited`. Editors that write to
`block_indices` must call it. See [Palette Utilities](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/palette_utils.md).

`lazy_source.palette_pristine` is cleared by `ResolveOrAppend`, `ResolveAir`, and `AppendPaletteEntry`. It is cleared by `MarkEdited` as well. It is not cleared by `ReplacePaletteEntry`; that editor leaves the block indices unchanged.

A region that has never been touched has `edited == false` and 
`palette_pristine == true`.

## Litematica passthrough

`LitematicaHandler::Encode` takes the passthrough path when all of the following hold:

- `lazy_source.encoding == kLitematicaLongArray`.

- `lazy_source.palette_pristine` is true.

- `lazy_source.air_at_zero` is true.

- `PaletteBpb(reg.palette.size()) == lazy_source.bits_per_block`.

- `edited` is false.

- `lazy_source.raw_bytes` is not empty.

Under these conditions the encoder writes `lazy_source.raw_bytes` into `BlockStates` unchanged `air_at_zero` matters because the Litematica writer only handles palette index 0 being air in the passthrough path. If air is at a different index, the writer needs to reorder the palette and remap the indices, which requires the materialized array.

`PaletteBpb` is a small helper in the handler that returns `max(2, bit_width(palette_size - 1))`. The comparison ensures the packed width is still consistent with the current palette size.

## Sponge passthrough

`SchemHandler::Encode` takes the passthrough path when:

- `lazy_source.encoding == kSpongeVarint`.

- `lazy_source.palette_pristine` is true.

- `lazy_source.raw_bytes` is not empty.

Under these conditions the encoder writes `lazy_source.raw_bytes` into `BlockData` (v2) or `Blocks/Data` (v3) unchanged.

Sponge has no air-at-zero requirement for passthrough, but the palette itself must still match the packed data. `palette_pristine` covers this.

## Why MarkEdited is required

Editors that write to `block_indices` change the array. If neither `edited` nor `palette_pristine` is cleared, the encoder does not know this and re-emits the original compressed form. The edit is silently lost.

`ResolveOrAppend` and `ResolveAir` clear `palette_pristine` when they append, which happens to also block passthrough. That is incidental. The documented contract is that every editor that writes to `block_indices` calls `MarkEdited`, regardless of which palette helpers it also used.

## Cross-format encoding

The passthrough condition is checked against the encoding of the `Region`. A Litematica region encoded as Sponge, or the reverse, never takes the passthrough path: the encodings differ. Such a region goes through `EnsureMaterialized` and a fresh pack.

## Remap and passthrough

`remap::internal::RemapRegionPalette` renames block ids in
`region.palette` through the active id-rename window. It rewrites
`BlockState::name` spans only. It does not touch `block_indices`, does not
clear `palette_pristine`, and does not call `MarkEdited`.

A region that takes the passthrough path before remap still takes it
after remap, as long as the target format matches the source encoding. The
Litematica conditions are unchanged: the rename table does not change
`palette.size()` (so `PaletteBpb` is stable) and does not change whether
`palette[0]` is an air variant (so `air_at_zero` is stable). The Sponge
conditions depend only on `encoding`, `palette_pristine`, and
`raw_bytes`.

The encoders read `region.palette` at serialization time. The palette
section of the output carries the renamed ids; the packed block data is
the bytes from decode. See [DataVersion Remap](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/remap.md).

Entity and block entity NBT rewrite is independent of this path. Those
fields are serialized from their IR values on every encode; the remap
pass replaces the spans, and the encoder reads them.

## After materialization

If a region was materialized for any reason but not edited, `edited` and `palette_pristine` are unchanged. The encoder can still take the passthrough path because the packed bytes in `lazy_source` are still consistent with `palette`.

Reading `block_indices` does not modify these flags.
