# Tool

The tool system has three parts: filters and views for selection, editors for mutation, and inspectors for analysis.

---

## 1. Filters & Views

A filter is a callable that satisfies `fschema::filters::Filter`. It is called with a `LocalPos` and a palette index, and returns whether the block at that position belongs to the selection.

A view pairs a `Region` with a filter. `BasicView<F>` walks the region's blocks, invokes the filter on each one, and passes only the matches to the callback. The view holds a pointer to the region; it does not own the region and does not copy block data. Because the filter type is a template parameter of `BasicView`, filters composed with `&&`, `||`, and `!` produce a concrete composite type that the compiler can inline into the loop.

### Core Components

- [View & Concepts](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/view.md): `BasicView`, `MakeView`, and the `Filter` concept.

- [Position & Utilities](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/pos.md): `LocalPos`, `LinearIndex`, and the axis enums.

- [Composite Filters](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/composite_filter.md): boolean composition of filters.

### Geometric and Spatial Filters

- [BoxFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/box_filter.md)

- [SphereFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/sphere_filter.md)

- [AxisFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/axis_filter.md)

- [NeighborFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/neighbor_filter.md)

### Block-State and Property Filters

- [BlockNameFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/block_name_filter.md) and [IsAirFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/block_name_filter.md)

- [BlockStateFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/block_state_filter.md)

- [NbtFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/nbt_filter.md)

- [SurfaceFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/surface_filter.md)

- [RandomFilter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/random_filter.md)

---

## 2. Editors

Editors transform region data. Most take a `BasicView<F>` to select blocks and write directly to `Region::block_indices`. Extract, BoundingBoxTrim, Mirror, and Rotate return a new `Region` instead.

Any editor that mutates `Region::block_indices` in place must call `fschema::editors::MarkEdited(r)`. It sets `edited = true` and `palette_pristine = false`. If `palette_pristine` stays true, the encoder may take the passthrough path and re-emit the original packed bytes, discarding the edits.

Some editors also change the palette. `ResolveOrAppend` and `ResolveAir` in `palette_utils.h` handle the lookup and, if the name is absent, append the entry and clear `palette_pristine`. Editors that only call `ResolveOrAppend` and do not touch `block_indices` still need to call `MarkEdited` if they change block data through other means; when in doubt, call it.

### Built-in Editors

- [Fill and Delete](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/fill.md): set selected blocks to a named block, or to air.

- [Replace and ReplaceWith](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/replace.md): replace by name, or by a caller-provided palette-index mapping.

- [Copy and Move](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/copy.md): copy blocks from a view into a target region, or shift them within one region.

- [Extract](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/extract.md) and [BoundingBoxTrim](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/bounding_box_trim.md): copy a selection into a new compact `Region`.

- [Mirror & Rotate](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/mirror.md): geometric transforms with optional state remapping.

- [Hollow](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/hollow.md): remove internal blocks, leaving a one-block shell.

- [Overlay](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/overlay.md): place blocks above exposed surfaces.

- [EraseNbt](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/erase_nbt.md): rewrite BlockEntity NBT and remove selected fields.

- [Palette Utilities](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/palette_utils.md): `ResolveOrAppend`, `ResolveAir`, `MarkEdited`.

- [ReplacePaletteEntry](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/replace_palette_entry.md): rewrite a palette entry without changing `block_indices`.

---

## 3. Inspectors

Inspectors are read-only. They take a `View`, a `BasicView<F>`, or a `const Region&` and return statistics, metadata, or validation results. None of them modify the region.

### Built-in Inspectors

- [Count](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/inspector/count.md): count blocks matching a name within a view.

- [Diff](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/inspector/diff.md): compare two regions with identical bounds.

- [List Material](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/inspector/list_material.md): list palette entries with usage counts.

- [List Entity and Tile Entity](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/inspector/list_entity.md): extract entity and block entity data.

- [Floating Check](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/inspector/floating_check.md): detect floating blocks and entities.

- [Support Check](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/inspector/support_check.md): validate structural support rules.

- [Version Check](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/inspector/version_check.md): check Minecraft DataVersion compatibility.

---

## 4. Custom Tools

Custom filters, editors, and inspectors use the same C++20 concepts as the built-in tools.

- [Custom Filter](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/filter/custom_filter.md): implement a type or lambda satisfying `Filter`.

- [Custom Editor](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/editor/custom_editor.md): mutate `block_indices` safely and call `MarkEdited`.

- [Custom Inspector](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/inspector/custom_inspector.md): aggregate data from a `View`.
