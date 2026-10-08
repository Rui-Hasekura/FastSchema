# Parsing

`fschema` parses schematic files in three layers. Pick the layer that matches
what you need to do; do not mix them.

```
bytes
  │
  ├─ NBT        base/nbt/parse.h            NbtTag tree
  │
  ├─ Format     litematic/parse.h           litematic::Litematic
  │             schem/parse.h               schem::Schematic
  │
  └─ IR         ir/format_handler.h         ir::Schema
```

The format layer and the IR layer are zero-copy. Both hold a
`std::unique_ptr<std::vector<std::byte>> owner` and every `string_view` or
`span` inside points into that buffer. Destroying the owning object
invalidates all of them.

## Decompression

Every `.litematic` and `.schem` file on disk is gzip-compressed. Three
functions in `fschema/base/compressor.h` handle that:

```cpp
std::expected<std::vector<std::byte>, CompressorError>
DecompressGzipFile(const std::filesystem::path& path);

std::expected<std::vector<std::byte>, CompressorError>
DecompressGzip(std::span<const std::byte> compressed);

std::expected<std::vector<std::byte>, CompressorError>
CompressGzip(std::span<const std::byte> data);
```

`CompressorError` is a separate enum from `ParseError`. The two do not
interconvert. Callers that need to distinguish a bad gzip header from a bad
NBT payload must handle both.

## NBT Layer

`fschema/base/nbt/parse.h` exposes four functions:

```cpp
[[nodiscard]] ParseResult<NbtPayload> ParsePayload(ByteReader& reader,
                                                   TagType tag_type);
[[nodiscard]] ParseResult<NbtCompound> ParseCompound(ByteReader& reader);
[[nodiscard]] ParseResult<NbtList> ParseList(ByteReader& reader);
[[nodiscard]] ParseResult<NbtTag> ParseNbt(ByteReader& reader);
```

`ByteReader` is constructed from a byte span and `DecodeLimits`:

```cpp
auto unpacked = fschema::base::DecompressGzipFile(path);
if (!unpacked) {
  // Handle CompressorError.
  return;
}

std::span<const std::byte> bytes(*unpacked);
fschema::base::ByteReader reader(bytes, fschema::base::DecodeLimits{});

auto tag = fschema::base::ParseNbt(reader);
if (!tag) {
  // Handle ParseError.
  return;
}
```

`ParseNbt` parses one root tag. `NbtTag::payload` is a `std::variant` of
scalars, spans, and `std::unique_ptr<NbtCompound>` /
`std::unique_ptr<NbtList>`. Array and string payloads are views into the
input buffer, not copies.

This layer knows nothing about gzip and nothing about schematic semantics.
It is useful for inspecting a file's tag structure, writing NBT dump tools,
and debugging format issues.

## Litematic Layer

`fschema/litematic/parse.h` has one entry point:

```cpp
[[nodiscard]] ParseResult<Litematic> ParseLitematic(
    std::unique_ptr<std::vector<std::byte>> decompressed,
    const fschema::base::DecodeLimits& limits = {});
```

It takes decompressed bytes, not a path. Decompress first:

```cpp
auto unpacked = fschema::base::DecompressGzipFile(path);
auto owner =
    std::make_unique<std::vector<std::byte>>(std::move(*unpacked));

auto lit = fschema::litematic::ParseLitematic(std::move(owner));
if (!lit) {
  // Handle ParseError.
  return;
}
```

`Litematic` holds `metadata`, a `std::vector<Region>`, `version`, and
`data_version`. Version is checked against `kV5`, `kV6`, `kV7`; anything
else returns `UnsupportedVersion`.

`litematic::Region` stores block data as raw bits:

```cpp
struct Region {
  std::string_view name;
  std::array<std::int32_t, 3> position;
  std::array<std::int32_t, 3> size;  // may be negative
  std::vector<BlockState> palette;
  std::vector<Entity> entities;
  std::vector<TileEntity> tile_entities;
  // pending ticks, raw spans ...
  std::span<const std::byte> raw_block_states;
  std::uint32_t raw_block_states_bpb = 0;
};
```

`size` can be negative, indicating a flipped axis. `VolumeOf(size)` takes
absolute values and returns the block count.

Block indices are not unpacked during parsing. To get them, call:

```cpp
std::uint64_t volume = fschema::litematic::VolumeOf(region.size);
std::uint32_t bpb =
    fschema::litematic::internal::BitsPerBlock(region.palette.size());

auto indices = fschema::litematic::internal::UnpackIndicesFused(
    region.raw_block_states, bpb, volume, region.palette.size());
```

`UnpackIndicesFused` allocates its own buffer and does not take an arena.
It dispatches to an AVX2 kernel or a Highway fallback. `internal::` is not
a stable public API, but these two names are the ones the IR layer uses.

Fields without an IR equivalent are only reachable here:
`pending_block_ticks`, `pending_fluid_ticks`, `pending_block_entities`,
`pending_entities`, `preview_data`, and each region's `raw_compound`.

## Sponge Schematic Layer

`fschema/schem/parse.h` has two entry points:

```cpp
[[nodiscard]] std::expected<Schematic, ParseError> ParseSchematic(
    const std::filesystem::path& path);

[[nodiscard]] std::expected<Schematic, ParseError> ParseSchematicFromBytes(
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits = {});
```

`ParseSchematic(path)` calls `DecompressGzipFile` internally.
`ParseSchematicFromBytes` takes decompressed bytes, matching
`ParseLitematic`.

Both v2 and v3 are detected in `ParseRoot`: a root tag named `"Schematic"`
selects v2, otherwise the root is scanned for a `"Schematic"` child and v3
is assumed. Callers do not need to know which version a file uses.

`Schematic` stores properties as strings, not NBT:

```cpp
struct BlockState {
  std::string_view name;
  std::string_view properties;  // "k=v,k=v", may be empty
};
```

Block indices are varint-packed in `raw_block_data`. To decode:

```cpp
std::uint64_t volume = fschema::schem::VolumeOf(schematic);
auto indices = fschema::schem::internal::DecodeVarintArray(
    schematic.raw_block_data, volume, schematic.palette.size(),
    *schematic.arena);
```

`DecodeVarintArray` takes the arena owned by `Schematic`. The returned
`UnInitBuffer` points into that arena and is invalidated with the
`Schematic`.

`VolumeOf(schematic)` is `width * height * length`. `BiomeVolumeOf` returns
the same for v3 and `width * length` for v2, since v2 biomes are 2D.

## IR Layer

`fschema/ir/format_handler.h` unifies the two formats behind one type,
`ir::Schema`. See `conversion.md` for the encoding side.

```cpp
[[nodiscard]] ParseResult<Schema> DecodeFromFormat(
    SourceFormat fmt,
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits = {});
```

Before calling it, register the handlers:

```cpp
namespace firc = fschema::ir::format;

firc::FormatRegistry::Instance().Register(
    std::make_unique<firc::LitematicaHandler>());
firc::FormatRegistry::Instance().Register(
    std::make_unique<firc::SchemHandler>());
```

`FormatRegistry` is a function-local static singleton. Registering the same
handler twice replaces the pointer in the lookup table; the previous
instance is kept alive until the registry is destroyed. Calling
`DecodeFromFormat` without registering returns
`ParseError{UnsupportedVersion, "FormatHandlerNotFound", 0}`.

`DecodeFromFormat` checks the first two bytes for the gzip magic `1f 8b`
and decompresses in place if present. This differs from the format layers,
which require the caller to decompress first.

`ir::Schema` holds `regions`, `metadata`, `owner`, and `arena`. Each
`ir::Region`:

```cpp
struct Region {
  std::string_view name;
  BoundingBox bounds;
  std::array<std::int32_t, 3> position;
  std::array<std::int32_t, 3> size;
  std::vector<BlockState> palette;
  mutable memory::UnInitBuffer<std::uint16_t> block_indices;
  mutable LazyBlockData lazy_source;
  mutable bool is_materialized = false;
  mutable bool edited = false;
  std::vector<Entity> entities;
  std::vector<BlockEntity> block_entities;
  std::span<const std::byte> raw_compound;
};
```

`block_indices` is lazy. To access it:

```cpp
auto mat = fschema::ir::EnsureMaterialized(region, *schema.arena);
if (!mat) {
  // Handle ParseError.
  return;
}
// region.block_indices is now populated.
```

`EnsureMaterialized` takes `const Region&` because the fields it writes are
`mutable`. It dispatches on `lazy_source.encoding` to the Litematic or
Sponge unpacker. Any code path that goes through `MakeView` triggers this
automatically; manual materialization is only needed when reading
`block_indices` directly.

`palette` properties use `PropertyEncoding` to record which format they
came from:

- `kNbt`: Litematica. `raw_properties` is a raw NBT Compound.
- `kString`: Sponge. `raw_properties` is `k=v,k=v` bytes.
- `kNone`: no properties.

The encoding is preserved so that `EncodeToFormat` can round-trip back to
the same format without a normalization step.

## Choosing a Layer

| Goal                                                     | Use                           |
| -------------------------------------------------------- | ----------------------------- |
| Inspect raw NBT structure                                | NBT layer                     |
| Read Litematic-only fields (pending ticks, preview data) | Litematic layer               |
| Read Sponge-only fields (biomes, required mods)          | Schem layer                   |
| Convert formats                                          | IR layer, see `conversion.md` |
| Apply filters, editors, inspectors                       | IR layer                      |

Converting a `litematic::Litematic` or a `schem::Schematic` into an
`ir::Schema` is not a public operation. The conversion lives in
`LitematicaHandler::DecodeFromParsed` and `SchemHandler::DecodeFromParsed`.
Those are only reachable through `DecodeFromFormat`, which requires the
original bytes.

## Errors

Every parse call returns `ParseResult<T>`, which is
`std::expected<T, ParseError>`. `ParseError` carries:

- `code`: an enum, e.g. `Truncated`, `InvalidTagId`, `UnsupportedVersion`.
- `path`: a breadcrumb such as `"Regions/main/BlockStates"`. May be empty.
- `offset`: byte offset in the source buffer.

`ToString(code)` returns a short string. `ToString(ParseError)` forwards to
`ToString(code)` and drops `path` and `offset`.

`DecodeFromFormat` wraps a gzip failure as
`ParseError{Truncated, "AutoDecompressFailed", 0}`, discarding the original
`CompressorError`. Callers that need the specific compression failure must
decompress before calling and pass the raw bytes.
