# Architecture

This page is for maintainers and for users who need to reason about
lifetime and ownership across layers. Normal callers do not need it.

## Layers

```
base/       byte I/O, gzip, error types, limits
base/nbt/   NBT reader, writer, skip, parse, tags
litematica/ Litematica-specific parser and types
sponge/     Sponge Schematic parser and types
ir/         format-agnostic Schematic and Region
remap/      cross-version data rewrite pass
filters/    selection
editors/    mutation
inspectors/ read-only analysis
memory/     Arena and UnInitBuffer
```

The `base/`, `litematica/`, `sponge/`, and `memory/` layers do not depend
on `ir/`. The `ir/` layer depends on all of them. The `remap/` layer
depends on `ir/` and on `base/nbt/`; it does not depend on `filters/`,
`editors/`, or `inspectors/`. `ir/format/` depends on `remap/` because
`Convert` calls `remap::MaybeRemapForTarget` between decode and encode.
Filters, editors, and inspectors depend on `ir/`.

## Ownership

Two object graphs own bytes:

- A format-layer object (`litematica::Litematica`,
  `sponge::Schematic`) owns a `std::unique_ptr<std::vector<std::byte>>`
  called `owner`. Every `string_view` and `span` inside the graph points
  into that buffer.
- The IR layer object (`ir::Schematic`) owns the same buffer and an
  additional `std::unique_ptr<memory::Arena>`. `Schematic::owner` is the
  same buffer the format layer produced; `Schematic::arena` holds
  materialized block indices and any arena-allocated property spans.
  `Schematic::regions` reference `owner` bytes and `arena` allocations. Moving
  a `Schematic` moves both pointers and leaves the spans valid. Copying is
  not defined.

The remap pass adds a third source of spans. Rewritten entity and block
entity NBT is serialized to a `std::vector<std::byte>` and copied into
`Schematic::arena` by `RemapContext::PersistBytes`. The resulting
`std::span<const std::byte>` replaces the corresponding `raw_nbt` field
and stays valid for the lifetime of the `Schematic`, like every other arena
allocation. Roots that the remap pre-scan does not hit keep their original
spans into `Schematic::owner`.

## Lazy fields on `ir::Region`

```cpp
struct Region {
  std::string_view name;
  BoundingBox bounds;
  std::array<std::int32_t, 3> raw_position;
  std::array<std::int32_t, 3> raw_size;

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
`mutable` because `EnsureMaterialized` takes `const Region&`. See
[Materialization](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/materialization.md).

## Format handlers

`ir::format::FormatHandler` is an interface with two operations, `Decode`
and `Encode`. `LitematicaHandler` and `SpongeHandler` implement it.
`FormatRegistry` is a function-local static that maps a `SourceFormat` to
a handler.

`DecodeFromFormat` finds the handler, detects gzip in the first two bytes,
decompresses if needed, and calls `Decode`. `EncodeToFormat` calls `Encode`
and compresses on request. Neither calls into `remap/`.

`Convert` is the only entry point that runs the remap pass. It decodes,
calls `remap::MaybeRemapForTarget`, then encodes. The pass may rewrite
palette names, block entity NBT, and entity NBT, and it updates
`schematic.data_version` and `schematic.source_version`. See
[Conversion](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/conversion.md)
and [DataVersion Remap](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/remap.md).

## Memory

`memory::Arena` is a slab-based bump allocator. It never frees an
individual allocation; `Reset` or destruction releases the whole arena.
`UnInitBuffer<T>` allocates from an arena when given one, and from the
global allocator otherwise. Destructors distinguish the two sources.

Allocation alignment is at least 64 bytes for `UnInitBuffer` elements.
Large slabs use `madvise(MADV_HUGEPAGE)` on Linux and large pages on
Windows when the privilege is available.

## Where to look next

- [Materialization](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/materialization.md)

- [Passthrough Encoding](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/passthrough.md)

- [DataVersion Remap](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/remap.md)

- [Parsing](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/parsing.md)
