# Parsing

`fschema` parses schematic files in three layers. Pick the layer that matches
what you need.

```
bytes
  │
  ├─ NBT        base/nbt/parse.h            NbtTag tree
  │
  ├─ Format     litematica/parse.h          litematica::Litematica
  │             schem/parse.h               schem::Schematic
  │
  └─ IR         ir/format_handler.h         ir::Schema
```

The format layer and the IR layer are zero-copy. Both hold a
`std::unique_ptr<std::vector<std::byte>> owner`; every `string_view` or
`span` inside points into that buffer. Destroying the owning object
invalidates all of them.

## Decompression

`.litematic` and `.schem` files on disk are gzip-compressed. The functions
in `fschema/base/compressor.h` handle compression:

```cpp
std::expected<std::vector<std::byte>, CompressorError>
DecompressGzipFile(const std::filesystem::path& path);

std::expected<std::vector<std::byte>, CompressorError>
DecompressGzip(std::span<const std::byte> compressed);

std::expected<std::vector<std::byte>, CompressorError>
CompressGzip(std::span<const std::byte> data);
```

`CompressorError` is separate from `ParseError`. The IR layer auto-detects  gzip and converts a compression failure into a `ParseError` with path `"AutoDecompressFailed"`. Callers that need the specific `CompressorError` must decompress before calling `DecodeFromFormat`.

## NBT Layer

`fschema/base/nbt/parse.h` exposes:

```cpp
[[nodiscard]] ParseResult<NbtPayload> ParsePayload(ByteReader& reader,
                                                   TagType tag_type);
[[nodiscard]] ParseResult<NbtCompound> ParseCompound(ByteReader& reader);
[[nodiscard]] ParseResult<NbtList> ParseList(ByteReader& reader);
[[nodiscard]] ParseResult<NbtTag> ParseNbt(ByteReader& reader);
```

`ByteReader` is constructed from a byte span and `DecodeLimits`:

```cpp
#include "fschema/base/nbt/parse.h"
#include "fschema/base/nbt/reader.h"

std::span<const std::byte> bytes = /* decompressed NBT */;
fschema::base::ByteReader reader(bytes, fschema::base::DecodeLimits{});

auto tag = fschema::base::ParseNbt(reader);
if (!tag) {
  const fschema::ParseError& e = tag.error();
  // e.code, e.path, e.offset
  return;
}
```

`NbtTag::payload` is a `std::variant` of scalars, spans, and 
`std::unique_ptr<NbtCompound>` / `std::unique_ptr<NbtList>`. Array and string payloads are views into the input buffer.

This layer is useful for inspecting raw tag structure. It has no knowledge of schematic semantics.

## Litematica Layer

`fschema/litematica/parse.h` has one entry point:

```cpp
[[nodiscard]] ParseResult<Litematica> ParseLitematica(
    std::unique_ptr<std::vector<std::byte>> decompressed,
    const fschema::base::DecodeLimits& limits = {});
```

It takes decompressed bytes. Decompress first:

```cpp
#include "fschema/base/compressor.h"
#include "fschema/litematica/parse.h"

auto unpacked = fschema::base::DecompressGzipFile("input.litematic");
if (!unpacked) {
  const fschema::base::CompressorError& e = unpacked.error();
  return;
}
auto owner =
    std::make_unique<std::vector<std::byte>>(std::move(*unpacked));

auto lit = fschema::litematica::ParseLitematica(std::move(owner));
if (!lit) {
  const fschema::ParseError& e = lit.error();
  return;
}
```

`Litematica` holds `metadata`, a `std::vector<Region>`, `version`, and 
`data_version`. Version is checked against `kV5`, `kV6`, `kV7`; any other value returns `UnsupportedVersion`.

`litematica::Region` stores block data as raw bits. `size` may be negative. 
`VolumeOf(size)` returns the absolute block count. Block indices are not unpacked during parsing; use `internal::UnpackIndicesFused` if needed. Fields without an IR equivalent, such as pending ticks and preview data, are reachable only at this layer.

## Sponge Schematic Layer

`fschema/schem/parse.h` has two entry points:

```cpp
[[nodiscard]] std::expected<Schematic, ParseError> ParseSchematic(
    const std::filesystem::path& path);

[[nodiscard]] std::expected<Schematic, ParseError> ParseSchematicFromBytes(
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits = {});
```

`ParseSchematic(path)` decompresses internally.

`ParseSchematicFromBytes` takes decompressed bytes.

Both v2 and v3 are detected in `ParseRoot`. A root tag named `"Schematic"` selects v2; otherwise the root is scanned for a `"Schematic"` child and v3 is assumed.

`Schematic` stores properties as strings. Block indices are varint-packed in 
`raw_block_data`. Use `internal::DecodeVarintArray` to decode them.

## IR Layer

`fschema/ir/format_handler.h` unifies both formats behind `ir::Schema`.

```cpp
[[nodiscard]] ParseResult<Schema> DecodeFromFormat(
    SourceFormat fmt,
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits = {});
```

Register handlers before calling:

```cpp
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematica_handler.h"
#include "fschema/ir/schem_handler.h"

namespace firc = fschema::ir::format;

firc::FormatRegistry::Instance().Register(
    std::make_unique<firc::LitematicaHandler>());
firc::FormatRegistry::Instance().Register(
    std::make_unique<firc::SchemHandler>());
```

`DecodeFromFormat` checks the first two bytes for the gzip magic `1f 8b`  and decompresses in place. This differs from the format layers, which require the caller to decompress first.

`ir::Schema` holds `regions`, `metadata`, `owner`, and `arena`. Each 
`ir::Region` has `bounds`, `position`, `size`, `palette`,  `block_indices`, `lazy_source`, `is_materialized`, `edited`, `entities`, 
`block_entities`, and `raw_compound`.

`block_indices` is lazy. Use `EnsureMaterialized` or `MakeView` before reading it.

```cpp
#include "fschema/ir/materialize.h"

auto mat_res = fschema::ir::EnsureMaterialized(region, *schema.arena);
if (!mat_res) {
  const fschema::ParseError& e = mat_res.error();
  return;
}
// region.block_indices is now populated.
```

`palette` properties use `PropertyEncoding`:

- `kNbt`: Litematica. Raw NBT Compound.

- `kString`: Sponge. `k=v,k=v` bytes.

- `kNone`: no properties.

The encoding is preserved so `EncodeToFormat` can round-trip without normalization.

## Choosing a Layer

| Goal                                                      | Use                           |
| --------------------------------------------------------- | ----------------------------- |
| Inspect raw NBT structure                                 | NBT layer                     |
| Read Litematica-only fields (pending ticks, preview data) | Litematica layer              |
| Read Sponge-only fields (biomes, required mods)           | Schem layer                   |
| Convert formats                                           | IR layer, see `conversion.md` |
| Apply filters, editors, inspectors                        | IR layer                      |

Converting a format-layer object into `ir::Schema` is not a public  
operation. The conversion lives in `LitematicaHandler::DecodeFromParsed`  
and `SchemHandler::DecodeFromParsed`, reachable only through  
`DecodeFromFormat`.

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
