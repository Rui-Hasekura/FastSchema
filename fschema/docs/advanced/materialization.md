# Materialization

`Region::block_indices` is not populated when a schematic is decoded.
Block indices live in `Region::lazy_source` in their on-disk form until
they are needed.

## LazyBlockData

```cpp
struct LazyBlockData {
  std::span<const std::byte> raw_bytes;
  BlockDataEncoding encoding = BlockDataEncoding::kNone;
  std::uint32_t bits_per_block = 0;
  std::size_t palette_size = 0;
  bool air_at_zero = false;
  bool palette_pristine = true;
};
```

`raw_bytes` points into `Schematic::owner`. It is not owned by the region.

`encoding` is one of:

- `kLitematicaLongArray`: bit-packed LongArray, LSB-first, with blocks that may straddle a 64-bit boundary.

- `kSpongeVarint`: 7-bit varint ByteArray, low group first, MSB as the continuation flag.

- `kNone`: no data, an empty region.

`bits_per_block` is used only for `kLitematicaLongArray`.

`palette_size` is the palette size at decode time. It is used for range validation.

`air_at_zero` is set when palette index 0 is an air variant. Litematica requires this for the passthrough path.

`palette_pristine` is set to false by palette helpers when a new entry is appended. See [Passthrough Encoding](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/passthrough.md).

## EnsureMaterialized

```cpp
[[nodiscard]] ParseResult<void> EnsureMaterialized(const Region& reg,
                                                   memory::Arena& arena);
```

`EnsureMaterialized` returns immediately if `reg.is_materialized` is true. Otherwise it computes the volume from `reg.bounds`, dispatches on 
`reg.lazy_source.encoding`, and assigns the result to `reg.block_indices`. It sets `reg.is_materialized = true` on success.

On failure, `lazy_source` and `is_materialized` are left untouched.

`EnsureMaterialized` takes `const Region&` because the fields it writes are `mutable`. The arena is passed in by the caller. For a `Schematic`, the usual arena is `*schematic.arena`.

## Dispatch

For `kLitematicaLongArray`, the call is 
`litematica::internal::UnpackIndicesFused`. It allocates an `UnInitBuffer` sized to the volume and runs a SIMD kernel.

For `kSpongeVarint`, the call is `sponge::internal::DecodeVarintArray`. It has three paths:

- Single-byte fast path when `palette_size <= 128`.

- Two-byte uniform fast path when `128 < palette_size <= 16384` and the data size allows it.

- Chunked varint decode otherwise.

For `kNone`, an uninitialized buffer is allocated. Every element is garbage. A region in this state has no valid blocks and should not be iterated.

## When materialization happens

- `MakeView` calls `EnsureMaterialized` if `is_materialized` is false, and propagates the error.

- Editors that take a view rely on the view for materialization. Editors that operate on a raw region call `EnsureMaterialized` themselves. `Hollow` and `Overlay` are examples.

- `Copy` materializes the target region, not the source.

- `EncodeToFormat` calls `EnsureMaterialized` only when it cannot take the passthrough path.

## NBT materialization

The remap pass parses some entity and block entity NBT into an
`NbtCompound` tree. This is a different operation from
`EnsureMaterialized`.

`remap::internal::MaterializeCompound` in
`fschema/remap/internal/nbt_utils.h` takes a raw NBT body span and returns
an `NbtCompound`. The pass transforms the tree and serializes it back with
`SerializeCompoundBody`. The result is copied into `Schematic::arena` by
`RemapContext::PersistBytes`. This flow runs per root, and only for roots
that a byte-level pre-scan identified as containing data for an active
rule family.

It does not read or write `Region::block_indices`, `Region::lazy_source`,
or `Region::is_materialized`. The block data stays in its packed form.
See [DataVersion Remap](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/remap.md).

## Iterating unmaterialized

Reading `region.block_indices` without materializing compiles and returns an empty buffer. Code that does this sees zero blocks instead of the expected data. This is not an error path; the check is not implemented on the buffer access.

Always call `EnsureMaterialized` or go through `MakeView` before reading 
`block_indices`.

## Lifetime of the materialized buffer

`block_indices` is an `UnInitBuffer<std::uint16_t>`. When the materialization ran with an arena, the buffer points into that arena. When it ran without an arena, the buffer is a direct allocation owned by the `UnInitBuffer`.

In both cases the buffer is invalidated when the region is destroyed, or when the arena is reset or destroyed.

## Re-materialization

Setting `reg.is_materialized = false` does not free the current buffer. The next `EnsureMaterialized` call reallocates from the same arena and overwrites `block_indices`.
