# Architecture

This page is for maintainers and for users who need to reason about
lifetime and ownership across layers. Normal callers do not need it.

## Layers

```
base/       byte I/O, gzip, error types, limits
base/nbt/   NBT reader, writer, skip, parse, tags
litematica/ Litematica-specific parser and types
schem/      Sponge Schematic parser and types
ir/         format-agnostic Schema and Region
filters/    selection
editors/    mutation
inspectors/ read-only analysis
memory/     Arena and UnInitBuffer
```

The `base/`, `litematica/`, `schem/`, and `memory/` layers do not depend
on `ir/`. The `ir/` layer depends on all of them. Filters, editors, and
inspectors depend on `ir/`.

## Ownership

Two object graphs own bytes:

- A format-layer object (`litematica::Litematica`,
  `schem::Schematic`) owns a `std::unique_ptr<std::vector<std::byte>>`
  called `owner`. Every `string_view` and `span` inside the graph points
  into that buffer.
- The IR layer object (`ir::Schema`) owns the same buffer and an
  additional `std::unique_ptr<memory::Arena>`. `Schema::owner` is the
  same buffer the format layer produced; `Schema::arena` holds
  materialized block indices and any arena-allocated property spans.
  `Schema::regions` reference `owner` bytes and `arena` allocations. Moving
  a `Schema` moves both pointers and leaves the spans valid. Copying is
  not defined.

## Lazy fields on `ir::Region`

```cpp
struct Region {
  std::string_view name;
  BoundingBox bounds;
  std::array<std::int32_t, 3> position;
  std::array<std::int32_t, 3> size;

  std::vector<BlockState> palette;
  mutable std::vector<std::uint8_t> is_air_lut;
  mutable memory::UnInitBuffer<std::uint16_t> block_indices;

  mutable LazyBlockData lazy_source;
  mutable bool is_materialized = false;
  mutable bool edited = false;

  std::vector<Entity> entities;
  std::vector<BlockEntity> block_entities;
  std::span<const std::byte> raw_compound;
};
```

`is_materialized`, `block_indices`, `is_air_lut`, and `lazy_source` are 
`mutable` because `EnsureMaterialized` takes `const Region&`. See [Materialization](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/materialization.md).

## Format handlers

`ir::format::FormatHandler` is an interface with two operations, `Decode` and `Encode`. `LitematicaHandler` and `SchemHandler` implement it. `FormatRegistry` is a function-local static that maps a `SourceFormat` to a handler.

`DecodeFromFormat` finds the handler, detects gzip in the first two bytes, decompresses if needed, and calls `Decode`. `EncodeToFormat` calls `Encode` and compresses on request.

## Memory

`memory::Arena` is a slab-based bump allocator. It never frees an individual allocation; `Reset` or destruction releases the whole arena. `UnInitBuffer<T>` allocates from an arena when given one, and from the global allocator otherwise. Destructors distinguish the two sources.

Allocation alignment is at least 64 bytes for `UnInitBuffer` elements. Large slabs use `madvise(MADV_HUGEPAGE)` on Linux and large pages on Windows when the privilege is available.

## Where to look next

- [Materialization](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/materialization.md)

- [Passthrough Encoding](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/passthrough.md)

- [Parsing](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/parsing.md)
