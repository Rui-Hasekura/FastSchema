# Conversion

Format conversion goes through `ir::Schema`. The entry points are in
`fschema/ir/format_handler.h`.

```cpp
[[nodiscard]] ParseResult<Schema> DecodeFromFormat(
    SourceFormat fmt,
    std::unique_ptr<std::vector<std::byte>> bytes,
    const base::DecodeLimits& limits = {});

[[nodiscard]] ParseResult<std::vector<std::byte>> EncodeToFormat(
    SourceFormat fmt,
    const Schema& ir,
    const EncodeOptions& options = {});

[[nodiscard]] ParseResult<std::vector<std::byte>> Convert(
    SourceFormat from, SourceFormat to,
    std::unique_ptr<std::vector<std::byte>> bytes,
    const EncodeOptions& options = {},
    const base::DecodeLimits& limits = {});
```

`SourceFormat` is `kLitematica` or `kSchem`. See `parsing.md` for the
decode side; this document covers encoding and the combined `Convert`
path.

## Registry

Handlers are looked up through a singleton:

```cpp
namespace firc = fschema::ir::format;

firc::FormatRegistry::Instance().Register(
    std::make_unique<firc::LitematicaHandler>());
firc::FormatRegistry::Instance().Register(
    std::make_unique<firc::SchemHandler>());
```

Register once, typically at startup. `FormatRegistry::Instance()` is a
function-local static; registration is thread-safe for the lifetime of the
process. If a lookup fails, `EncodeToFormat` returns
`ParseError{UnsupportedVersion, "FormatHandlerNotFound", 0}`.

## EncodeOptions

```cpp
struct EncodeOptions {
  std::int32_t target_version = 0;

  enum class MultiRegionStrategy : std::uint8_t {
    kStrictSingle = 0,
    kExtractFirst = 1,
    kExtractByName = 2,
    kMergeBoundingBox = 3,
  };
  MultiRegionStrategy multi_region = MultiRegionStrategy::kStrictSingle;

  std::string_view extract_region_name;
  std::string_view fill_block = "minecraft:air";
  bool compress = false;
};
```

`target_version` is the format version to write. For Litematica it is 5, 6,
or 7; for Sponge it is 2 or 3. A value of `0` selects a version from
`ir.data_version`: 1205 or later maps to Litematica 7, 118 to 6, earlier
to 5. Sponge defaults to 3.

The `multi_region` field only applies when the target is Sponge and the
schema has more than one region. Litematica handles multiple regions
natively, so the field is ignored for that target.

`extract_region_name` is read only when `multi_region` is
`kExtractByName`.

`fill_block` is the block name used to pad the merged region when
`multi_region` is `kMergeBoundingBox`. It is written to palette index 0 of
the merged region.

`compress` controls gzip on the output. The default is `false`, so
`EncodeToFormat` writes raw NBT unless the caller opts in. `Convert`
inherits the same default.

## Multi-Region Strategies

When the target is Sponge and `ir.regions` has more than one entry, the
strategy decides what happens:

- `kStrictSingle`: return `UnsupportedVersion` with path `"MultiRegion"`.
  This is the default, and it forces the caller to make an explicit choice.
- `kExtractFirst`: use `ir.regions[0]` and ignore the rest.
- `kExtractByName`: use the region whose `name` matches
  `extract_region_name`. If no region matches, return `MissingField` with
  path `"ExtractRegion"`.
- `kMergeBoundingBox`: compute the union of every region's bounding box,
  allocate a new region of that size filled with `fill_block`, and copy
  each region's blocks into it. Palette entries are deduplicated by
  `(name, raw_properties)`. Entities and block entities from all regions are
  concatenated.

The merge allocates its own arena, which lives only as long as the encode
call. The intermediate region is not returned to the caller.

## Version Selection

`EncodeToFormat` writes the target version into the output header:

- Litematica: field `Version`, plus `SubVersion = 1` when the version is 7.
- Sponge: field `Version`, and the root layout switches between v2 (flat
  root compound) and v3 (nested under `"Schematic"`).

For Litematica, when the caller's `target_version` equals
`ir.source_version`, the encoder writes extension fields from
`ir.metadata.raw_compound` and each region's `raw_compound`. This preserves
fields the IR does not model. When the versions differ, or the schema did
not originate from Litematica, the encoder writes empty defaults for those
fields instead.

The same logic applies to Sponge via `ShouldUseExtensions`, keyed on
whether the requested major version matches `ir.source_version`.

## Gzip on the Output

`EncodeToFormat` compresses the result only when `options.compress` is
true:

```cpp
auto result = h->Encode(ir, options);
if (!result) return std::unexpected(result.error());

if (options.compress) {
  auto compressed = base::CompressGzip(std::span<const std::byte>(*result));
  if (!compressed) {
    return std::unexpected(
        ParseError{ParseError::Code::Truncated, "CompressFailed", 0});
  }
  return *compressed;
}
return result;
```

On failure the `CompressorError` is discarded and replaced with a generic
`ParseError{Truncated, "CompressFailed", 0}`. Callers that need the
specific compression failure must call `CompressGzip` themselves.

## Format-Specific Behaviour

### Litematica Output

The encoder writes `Metadata`, `Regions`, and for each region `Position`,
`Size`, `BlockStatePalette`, `BlockStates`, `TileEntities`, `Entities`, and
either the preserved extension fields or empty pending tick lists.

Two things affect the output size:

- Litematica requires palette index 0 to be air. If the region's palette
  already has an air variant at index 0, the block indices are packed as-is.
  If air is at another index, the palette is reordered and the indices are
  remapped with a swap. If air is absent entirely, a new entry is prepended
  and every index is shifted by one.
- The bit width per block is derived from the final palette size:
  `max(2, bit_width(palette_size - 1))`.

The encoder can skip repacking when the region has not been edited and the
palette is pristine: it writes `lazy_source.raw_bytes` directly. This is why
editors must call `MarkEdited`; without it, the passthrough path discards
the edit.

If the region's palette exceeds 65536 entries, the encoder returns
`OversizedPayload` with path `"PaletteOverflow"`.

### Sponge Output

Dimensions are checked against `int16` range before writing. Out-of-range
values produce `OversizedPayload` with path `"DimensionOverflow"`.

The palette is written as a compound whose keys are full block state
strings. For `kString` properties the raw `k=v,k=v` bytes are wrapped in
brackets. For `kNbt` properties the encoder converts the raw NBT Compound
to a `k=v,k=v` string via `internal::NbtPropsToString` first. This is a
one-way conversion; the NBT structure of the properties is not preserved
when writing Sponge output.

Block data is written as a varint ByteArray. The encoder computes an upper
bound with `internal::VarintUpperBound` and packs into it. When the region
is unedited, the palette is pristine, and `lazy_source.encoding` is
`kSpongeVarint`, the raw bytes are written directly.

Entities and block entities are written with the same structure they
arrived in, or with an empty `Data` sub-compound for v3 when the IR does
not carry one.

## Convert

`Convert` is `DecodeFromFormat` followed by `EncodeToFormat`:

```cpp
auto ir_result = DecodeFromFormat(from, std::move(bytes), limits);
if (!ir_result) return std::unexpected(ir_result.error());
return EncodeToFormat(to, *ir_result, options);
```

The `owner` and `arena` inside the decoded `Schema` stay alive for the
duration of the call. Views and spans into the input bytes are valid
throughout.

## Example

```cpp
#include <filesystem>
#include <memory>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematic_handler.h"
#include "fschema/ir/schem_handler.h"

namespace fir = fschema::ir;
namespace firc = fschema::ir::format;

int main() {
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::LitematicaHandler>());
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::SchemHandler>());

  auto unpacked = fschema::base::DecompressGzipFile("input.litematic");
  if (!unpacked) return 1;

  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked));

  firc::EncodeOptions opts;
  opts.target_version = 3;
  opts.multi_region =
      firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;
  opts.compress = true;

  auto result = firc::Convert(
      fir::SourceFormat::kLitematica,
      fir::SourceFormat::kSchem,
      std::move(owner),
      opts);
  if (!result) return 1;

  // result is gzip-compressed Sponge v3 bytes.
  return 0;
}
```

Note that the input is decompressed before being handed to `Convert`.
`DecodeFromFormat` detects gzip and would decompress a compressed buffer as
well; the explicit call above is redundant but harmless. Passing the raw
file bytes directly is also valid, and slightly shorter:

```cpp
auto owner = std::make_unique<std::vector<std::byte>>(file_size);
// read file into owner ...
auto result = firc::Convert(
    fir::SourceFormat::kLitematica,
    fir::SourceFormat::kSchem,
    std::move(owner),
    opts);
```

Pick one form. Decompressing twice is not an error, but there is no reason
to do it.
