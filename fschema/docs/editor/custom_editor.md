# Custom Editor

A custom editor is a function template that takes a `BasicView<F>` and
performs a write. There is no base class. The contract is about what the
function does to the region, not about the function's signature.

## Skeleton

The typical shape follows the built-in editors:

```cpp
namespace fschema::editors {

template <filters::Filter F>
[[nodiscard]] ParseResult<void> Tint(filters::BasicView<F> v,
                                     std::string_view block_name) {
  ir::Region& r = v.region();

  // Resolve the palette index, appending if needed.
  const std::uint16_t target_idx = ResolveOrAppend(r, block_name);

  std::uint16_t* FSCHEMA_RESTRICT data = r.block_indices.data();
  v.for_each_linear([&](std::uint64_t linear_idx, std::uint16_t) {
    data[linear_idx] = target_idx;
  });

  MarkEdited(r);
  return {};
}

}  // namespace fschema::editors
```

Walking through the parts:

- `v.region()` returns `ir::Region&`, even from a const view. `BasicView`
  holds the region by pointer, and `region()` is `const` while returning a
  non-const reference. See `view.md`.
- `ResolveOrAppend` looks up the palette index for `block_name` and, if it
  is absent, appends a new entry. On append it clears
  `r.lazy_source.palette_pristine`. `ResolveAir` does the same for air.
- `for_each_linear` walks the selected blocks in YZX order and passes the
  linear index and the palette index to the callback. Use this when the
  callback only writes to `block_indices[linear_idx]`. Use `for_each` when
  the callback needs `LocalPos` as well.
- `MarkEdited` sets `edited = true` and clears `palette_pristine`. See
  below.
- `ParseResult<void>` is the conventional return type. Every built-in
  editor uses it because materialization and packing can fail. An editor
  that cannot fail may return `void`.

## Why MarkEdited matters

The encoder can take a passthrough path that re-emits `lazy_source.raw_bytes`
without unpacking and repacking. That path is chosen when, among other
conditions, `palette_pristine && !edited`.

`ResolveOrAppend` and `ResolveAir` clear `palette_pristine` when they
append a new palette entry, which already blocks the passthrough path.
`MarkEdited` additionally sets `edited = true`. Both flags are checked by
the encoder, and the documented contract for editors that write to
`block_indices` is to call `MarkEdited` regardless of what the palette
helpers did.

An editor that writes to `block_indices` without calling `MarkEdited` can
silently produce a file whose serialized block data does not reflect the
edit.

## Editors that do not call MarkEdited

Three patterns appear in the built-ins:

- Editors that return a new `Region` and only read the input. `Extract`,
  `Mirror`, and `Rotate` follow this pattern. The output region is newly
  constructed with `is_materialized = true`; there is nothing to mark on
  the input.
- Editors that rewrite `block_entities` but not `block_indices`.
  `EraseNbt` is the reference example. The encoder reads
  `block_entities` at serialization time, so there is no stale-data path
  to guard against.
- Editors that resolve a palette entry but do not write block data.
  `ReplacePaletteEntry` rewrites one palette entry and intentionally leaves
  `palette_pristine` alone, since `block_indices` is unchanged.

## Overlapping writes

If an editor moves blocks to positions that overlap the source, collect
the intended writes before performing any. `Move` is the reference
example: it reads every selected block into a `std::vector<Entry>` first,
then clears the source positions, then writes the destination values.
Writing during the scan would produce cascading updates.

The same concern applies to any editor that both reads and writes the
same region.

## Materialization

`MakeView` calls `EnsureMaterialized` if needed. If an editor needs to
materialize directly, as `Hollow` and `Overlay` do, call
`ir::EnsureMaterialized(r, arena)` and propagate the error:

```cpp
ir::Region& r = v.region();
auto mat_res = ir::EnsureMaterialized(r, arena);
if (!mat_res) return std::unexpected(mat_res.error());
```

`Copy` materializes the target region, not the source. The source is
already materialized because a view was made over it.
