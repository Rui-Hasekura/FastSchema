# Quick Start

> Assuming you have already completed the installation.

This walks through the shortest path from a file on disk to a modified file
on disk. It assumes the project is already built and linked, and that the
compiler supports C++23.

## Registration

Format handlers are looked up through a singleton. Register them once,
typically at program startup:

```cpp
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematic_handler.h"
#include "fschema/ir/schem_handler.h"

namespace firc = fschema::ir::format;

firc::FormatRegistry::Instance().Register(
    std::make_unique<firc::LitematicaHandler>());
firc::FormatRegistry::Instance().Register(
    std::make_unique<firc::SchemHandler>());
```

Skipping this step makes `DecodeFromFormat`, `EncodeToFormat`, and
`Convert` return `ParseError{UnsupportedVersion, "FormatHandlerNotFound", 0}`.

## Convert one format to another

`Convert` decodes and re-encodes in one call. It accepts raw file bytes,
compressed or not; `DecodeFromFormat` checks the gzip magic and
decompresses if needed.

```cpp
#include <cstddef>
#include <fstream>
#include <memory>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/ir/format_handler.h"

namespace fir = fschema::ir;
namespace firc = fschema::ir::format;

int main() {
  // Register once. In a real program this happens somewhere near startup.
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::LitematicaHandler>());
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::SchemHandler>());

  // Read the file into memory. The Schema will take ownership of this buffer.
  std::ifstream in("input.litematic", std::ios::binary | std::ios::ate);
  const auto size = in.tellg();
  auto owner = std::make_unique<std::vector<std::byte>>(size);
  in.seekg(0);
  in.read(reinterpret_cast<char*>(owner->data()), size);

  firc::EncodeOptions opts;
  opts.target_version = 3;  // Sponge v3
  opts.multi_region =
      firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;
  opts.compress = true;

  auto result = firc::Convert(
      fir::SourceFormat::kLitematica,
      fir::SourceFormat::kSchem,
      std::move(owner),
      opts);
  if (!result) {
    const fschema::ParseError& e = result.error();
    // e.code, e.path, e.offset
    return 1;
  }

  std::ofstream out("output.schem", std::ios::binary);
  out.write(reinterpret_cast<const char*>(result->data()), result->size());
  return 0;
}
```

`Convert` defaults to `compress = false`. Set it explicitly if you want the
output to be a `.schem` or `.litematic` file that other tools can read;
both formats expect gzip on disk.

## Read, modify, write

Editing goes through the IR layer. Decode first, then build a view over a
region, apply an editor, and encode the modified schema back out.

```cpp
#include <memory>
#include <vector>

#include "fschema/base/compressor.h"
#include "fschema/editors/fill.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematic_handler.h"
#include "fschema/memory/arena.h"

namespace fl = fschema::filters;
namespace ed = fschema::editors;
namespace fir = fschema::ir;
namespace firc = fschema::ir::format;

int main() {
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::LitematicaHandler>());

  // Read and decode.
  auto unpacked = fschema::base::DecompressGzipFile("input.litematic");
  if (!unpacked) return 1;
  auto owner =
      std::make_unique<std::vector<std::byte>>(std::move(*unpacked));

  auto schema_res =
      firc::DecodeFromFormat(fir::SourceFormat::kLitematica, std::move(owner));
  if (!schema_res) return 1;
  fir::Schema& schema = *schema_res;

  if (schema.regions.empty()) return 1;
  fir::Region& region = schema.regions[0];

  // Select a box in local coordinates.
  fl::BoxFilter box{0, 0, 0, 9, 9, 9};

  // MakeView materializes the region on demand and returns a ParseError if
  // that fails. The region must outlive the view.
  auto view_res = fl::MakeView(region, box, *schema.arena);
  if (!view_res) return 1;

  // Fill the selection. This writes to region.block_indices and calls
  // MarkEdited internally.
  auto fill_res = ed::Fill(*view_res, "minecraft:stone");
  if (!fill_res) return 1;

  // Encode back. Same format, so the encoder can reuse the source version
  // and any extension fields preserved in the IR.
  firc::EncodeOptions opts;
  opts.compress = true;

  auto encoded =
      firc::EncodeToFormat(fir::SourceFormat::kLitematica, schema, opts);
  if (!encoded) return 1;

  std::ofstream out("output.litematic", std::ios::binary);
  out.write(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  return 0;
}
```

## Key points

- `Schema` owns the decompressed byte buffer. All `string_view` and
  `span` fields in the IR point into it. Do not destroy the `Schema` while
  holding references into its regions.
- `Region::block_indices` is lazy. `MakeView` and any editor take care of
  materializing it. Reading `block_indices` directly requires calling
  `ir::EnsureMaterialized(region, *schema.arena)` first.
- Editors that write to `block_indices` call `MarkEdited` internally. If
  you write a custom editor, you must call it yourself; see
  `custom_editor.md`.
- Filters are plain types or lambdas. Combine them with `&&`, `||`, and
  `!`. See `filter.md` for the list.
- The encoder checks `edited` and `palette_pristine` on each region. If
  neither was cleared, it can write the original packed bytes and skip
  repacking entirely.

## Pitfalls

- Forgetting to register handlers. The error message is
  `"FormatHandlerNotFound"`, not something about the file.
- Passing compressed bytes to the format-layer parsers
  (`ParseLitematic`, `ParseSchematicFromBytes`). Those expect decompressed
  bytes. The IR-layer `DecodeFromFormat` is the one that auto-detects
  gzip.
- Reaching into `Region::block_indices` without materializing. It compiles
  and the buffer is empty.
- Holding a `std::string_view` from a palette entry after the `Schema` is
  destroyed. The view points into the schema's owned buffer.
- Confusing `CompressorError` and `ParseError`. They are separate enums.
  `Convert` discards the former and wraps it in the latter.

## What to read next

- [Parsing](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/parsing.md): the three parsing layers and when to use each.
- [Conversion](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/conversion.md): `EncodeOptions`, multi-region strategies,
  format-specific output behaviour.
- [Tool](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tool.md): filters, views, editors, inspectors, and how to write
  custom ones.
