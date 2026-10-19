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

`SourceFormat` is `kLitematica` or `kSchem`. See `parsing.md` for the decode side.

`Convert` is decode, optional DataVersion remap, encode. `DecodeFromFormat`
and `EncodeToFormat` do not remap on their own.

## Registry

Handlers are looked up through a singleton. Register them once at startup:

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

If a lookup fails, `EncodeToFormat` returns `ParseError{UnsupportedVersion, "FormatHandlerNotFound", 0}`.

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

  remap::RemapOptions remap;
  remap::RemapStats* remap_stats = nullptr;
};
```

`target_version` is the format version to write. Litematica: 5, 6, or 7.
Sponge: 2 or 3. A value of `0` selects from `ir.data_version`: 1205 or later
maps to Litematica 7, 118 to 6, earlier to 5. Sponge defaults to 3.

`multi_region` applies only when the target is Sponge and the schema has more
than one region. Litematica handles multiple regions natively, so the field is
ignored for that target.

`extract_region_name` is read only when `multi_region` is `kExtractByName`.

`fill_block` is the block name used to pad the merged region when
`multi_region` is `kMergeBoundingBox`. It is written to palette index 0 of the
merged region.

`compress` controls gzip on the output. Default is `false`.

`remap` controls the cross-version data pass that `Convert` runs between
decode and encode. Defaults leave the pass on its standard target resolution;
see DataVersion Remap below.

`remap_stats`, when non-null, receives counters, diagnostics, and a watchlist
from the pass. It is ignored when no remap runs.

## Multi-Region Strategies

When the target is Sponge and `ir.regions` has more than one entry:

- `kStrictSingle`: return `UnsupportedVersion` with path `"MultiRegion"`.
  This is the default.

- `kExtractFirst`: use `ir.regions[0]`.

- `kExtractByName`: use the region whose `name` matches
  `extract_region_name`. If none matches, return `MissingField` with path
  `"ExtractRegion"`.

- `kMergeBoundingBox`: compute the union of all region bounding boxes,
  allocate a new region filled with `fill_block`, and copy each region's
  blocks into it. Palette entries are deduplicated by
  `(name, raw_properties)`. Entities and block entities are concatenated.

The merge allocates its own arena, which lives only for the encode call.

## Version Selection

`EncodeToFormat` writes the target version into the output header:

- Litematica: `Version`, plus `SubVersion = 1` when version is 7.

- Sponge: `Version`, and root layout switches between v2 (flat) and v3
  (nested under `"Schematic"`).

For Litematica, when `target_version` equals `ir.source_version`, the encoder
writes extension fields from `ir.metadata.raw_compound` and each region's
`raw_compound`. When versions differ, it writes empty defaults.

The same logic applies to Sponge via `ShouldUseExtensions`.

## DataVersion Remap

`Convert` decodes to IR, then may rewrite the IR so its data semantics match
a target Minecraft DataVersion, then encodes. The remap step runs between
decode and encode.

The pass does not run from `DecodeFromFormat` or `EncodeToFormat`. A caller
that decodes, edits, and re-encodes without going through `Convert` keeps the
source `DataVersion` in the output.

### Target resolution

`remap::MaybeRemapForTarget` resolves the effective target DataVersion from
`options.remap.target_data_version` when it is non-zero. Otherwise it resolves
from the target container version:

| Target format | Target version | Resolved DataVersion | Behaviour                                                                                            |
| ------------- | -------------- | -------------------- | ---------------------------------------------------------------------------------------------------- |
| Litematica    | 5              | -1                   | `Convert` fails with `UnsupportedVersion`, message `"litematica v5 target rejected by remap policy"` |
| Litematica    | 6              | 3463 (1.20 anchor)   | Remaps when the source DV differs                                                                    |
| Litematica    | 7 or 0         | 0                    | No remap; round-trip                                                                                 |
| Schem         | any            | 0                    | No remap                                                                                             |

If the resolved target equals `schema.data_version`, the pass reports
`kNoOpAtTarget` and returns.

The rule set currently implements downgrades only. An upgrade request (target
DV above source DV) leaves the data and the version fields untouched and emits
`kNoRulesCoverage` in the stats.

### What the pass rewrites

- Palette block ids in `region.palette` are renamed through the id-rename
  table. `block_indices` and `palette_pristine` are not touched, so the
  encoders can still take their passthrough path.

- `block_entities` and `entities` are pre-scanned at the byte level. A root
  that does not contain a pattern for any active family keeps its original
  `raw_nbt` span. A hit root is parsed into an NBT tree, transformed, and
  re-serialized into `Schema::arena`.

- `schema.data_version` is set to the resolved target, and
  `schema.source_version` is set to `ContainerForDataVersion(target DataVersion)`.

Remap does not call `EnsureMaterialized`. `Region::block_indices` stays lazy.

Rule families, counters, and diagnostic codes are listed in
`advanced/remap.md`.

### Failures and diagnostics

Hard failures (`std::expected` error) occur on:

- a target the policy rejects (Litematica v5),
- a target DataVersion `<= 0`,
- a target container below 2860,
- IR corruption during the walk.

Everything else is a diagnostic. `Convert` returns encoded bytes even when the
pass emitted diagnostics. Callers that need the events read `remap_stats`
after the call.

## Gzip on the Output

`EncodeToFormat` compresses only when `options.compress` is true. On failure,
`CompressorError` is discarded and replaced with a generic
`ParseError{Truncated, "CompressFailed", 0}`. Callers that need the specific
compression failure must call `CompressGzip` themselves.

## Format-Specific Behaviour

### Litematica Output

Writes `Metadata`, `Regions`, and for each region `Position`, `Size`,
`BlockStatePalette`, `BlockStates`, `TileEntities`, `Entities`, and either
preserved extension fields or empty pending tick lists.

- Litematica requires palette index 0 to be air. If air is at another index,
  the palette is reordered and indices are remapped. If air is absent, a new
  entry is prepended and all indices shift by one.

- Bit width per block is `max(2, bit_width(palette_size - 1))`.

The encoder can skip repacking when `palette_pristine` is true and `edited`
is false. It writes `lazy_source.raw_bytes` directly. Editors must call
`MarkEdited` to prevent this. Palette renames from the remap step do not clear
`palette_pristine`; the packed indices are still valid.

If the palette exceeds 65536 entries, the encoder returns `OversizedPayload`
with path `"PaletteOverflow"`.

### Sponge Output

Dimensions are checked against `int16` range. Out-of-range values produce
`OversizedPayload` with path `"DimensionOverflow"`.

Palette keys are full block state strings. `kString` properties are wrapped
in brackets. `kNbt` properties are converted to `k=v,k=v` via
`internal::NbtPropsToString`. The NBT structure is not preserved.

Block data is written as a varint ByteArray. When the region is unedited, the
palette is pristine, and `lazy_source.encoding` is `kSpongeVarint`, the raw
bytes are written directly.

Entities and block entities are written with the same structure they arrived
in, or with an empty `Data` sub-compound for v3 when the IR does not carry one.

## Convert

`Convert` is decode, remap, encode:

```cpp
auto ir_result = DecodeFromFormat(from, std::move(bytes), limits);
if (!ir_result) return std::unexpected(ir_result.error());

if (auto r = remap::MaybeRemapForTarget(*ir_result, to,
                                        options.target_version,
                                        options.remap,
                                        options.remap_stats);
    !r) {
  return std::unexpected(r.error());
}

return EncodeToFormat(to, *ir_result, options);
```

The `owner` and `arena` inside the decoded `Schema` stay alive for the
duration of the call, including the remap step. Remapped entity and block
entity NBT is persisted in the same arena.

## Example

```cpp
#include <cstddef>
#include <fstream>
#include <memory>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematica_handler.h"
#include "fschema/ir/schem_handler.h"

namespace fir = fschema::ir;
namespace firc = fschema::ir::format;

int main() {
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::LitematicaHandler>());
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::SchemHandler>());

  std::ifstream in("input.litematic", std::ios::binary | std::ios::ate);
  if (!in) return 1;
  const auto size = in.tellg();
  if (size <= 0) return 1;
  auto bytes = std::make_unique<std::vector<std::byte>>(
      static_cast<std::size_t>(size));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(bytes->data()), size);
  if (!in) return 1;

  fschema::remap::RemapStats remap_stats;

  firc::EncodeOptions opts;
  opts.target_version = 3;
  opts.multi_region =
      firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;
  opts.compress = true;
  opts.remap_stats = &remap_stats;

  auto result = firc::Convert(
      fir::SourceFormat::kLitematica,
      fir::SourceFormat::kSchem,
      std::move(bytes),
      opts);
  if (!result) {
    const fschema::ParseError& e = result.error();
    // e.code, e.path, e.offset
    return 1;
  }

  // remap_stats now holds counters, diagnostics, and a watchlist.
  // See advanced/remap.md.

  std::ofstream out("output.schem", std::ios::binary);
  if (!out) return 1;
  out.write(reinterpret_cast<const char*>(result->data()), result->size());
  if (!out) return 1;
}
```

The input may be compressed or raw. `DecodeFromFormat` detects gzip and
decompresses if needed. Passing already-decompressed bytes is also valid.

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

To force a remap on a target that resolves to no remap (for example Sponge
or Litematica v7), set `opts.remap.target_data_version` to the desired
DataVersion. The resolution table above is bypassed.
