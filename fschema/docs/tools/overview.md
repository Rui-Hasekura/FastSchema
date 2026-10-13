# Tool API Overview

The tool API is split into three parts.

- Filters and views select blocks. A filter is a callable that decides
  whether a given block belongs to a selection. A view pairs a region with
  a filter and walks the region's blocks.
- Editors mutate a region. Most take a view to select blocks and write
  directly to `Region::block_indices`. Some return a new region instead.
- Inspectors read a region and return statistics, metadata, or validation
  results. They never modify the region.

## Pages

- [Views](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/views.md):
  `BasicView`, `MakeView`, `View`, `LocalPos`, `LinearIndex`, and the axis
  enums.
- [Filters](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters.md):
  the `Filter` concept and composite filters. Individual filters are in
  the `filters/` subdirectory.
- [Editors](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors.md):
  the shared editor contract and the built-in editors.
- [Inspectors](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors.md):
  the built-in inspectors.
- [Custom Tools](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/custom_tools.md):
  writing custom filters, editors, and inspectors.

## Filter pages

- [Spatial](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters/spatial.md):
  `BoxFilter`, `SphereFilter`, `AxisFilter`.
- [Block name and air](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters/block_name_air.md):
  `BlockNameFilter`, `IsAirFilter`.
- [Block state](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters/block_state.md):
  `BlockStateFilter`.
- [Neighbor and surface](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters/neighbor_surface.md):
  `NeighborFilter`, `SurfaceFilter`.
- [NBT](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters/nbt.md):
  `NbtFilter`.
- [Random](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/filters/random.md):
  `RandomFilter`.

## Editor pages

- [Fill and replace](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/fill_replace.md):
  `Fill`, `Delete`, `Replace`, `ReplaceWith`.
- [Copy and move](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/copy_move.md):
  `Copy`, `Move`.
- [Extract and trim](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/extract_trim.md):
  `Extract`, `BoundingBoxTrim`.
- [Mirror and rotate](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/mirror_rotate.md):
  `Mirror`, `Rotate`.
- [Hollow](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/hollow.md)
- [Overlay](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/overlay.md)
- [EraseNbt](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/erase_nbt.md)
- [Palette utilities](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/editors/palette_utils.md):
  `ResolveOrAppend`, `ResolveAir`, `MarkEdited`, `ReplacePaletteEntry`.

## Inspector pages

- [Listing](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors/listing.md):
  `Count`, `ListMaterial`, `ListEntity`, `ListTileEntity`.
- [Diff](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors/diff.md)
- [Floating and support](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors/floating_support.md):
  `FloatingCheck`, `SupportCheck`.
- [Version](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/inspectors/version.md):
  `FindIncompatibleBlocks`, `GetMinimumRequiredDataVersion`,
  `GetHighestSupportedDataVersion`.
