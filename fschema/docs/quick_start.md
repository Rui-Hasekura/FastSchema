# Quick Start

This page shows the shortest path from a file on disk to a modified file on
disk. It assumes the project is built and linked, and that the compiler
supports C++23.

## Register format handlers

Format handlers are looked up through a singleton. Register them once at
program startup.

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


Without registration, `DecodeFromFormat`, `EncodeToFormat`, and `Convert`  
return `ParseError{UnsupportedVersion, "FormatHandlerNotFound", 0}`.

## Read, edit, write

The IR layer is the usual entry point for editing. Decode the input, build a  
view over a region, apply an editor, and encode the result.

```cpp
#include <cstddef>
#include <fstream>
#include <memory>
#include <vector>

#include "fschema/base/error.h"
#include "fschema/editors/fill.h"
#include "fschema/filters/box_filter.h"
#include "fschema/filters/view.h"
#include "fschema/ir/format_handler.h"
#include "fschema/ir/litematica_handler.h"
#include "fschema/ir/schem_handler.h"
#include "fschema/memory/arena.h"

namespace fir = fschema::ir;
namespace firc = fschema::ir::format;
namespace fl = fschema::filters;
namespace ed = fschema::editors;

int main() {
  // Register handlers once at startup.
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::LitematicaHandler>());
  firc::FormatRegistry::Instance().Register(
      std::make_unique<firc::SchemHandler>());

  // Read the input file into memory.
  std::ifstream in("input.litematic", std::ios::binary | std::ios::ate);
  if (!in) return 1;
  const auto size = in.tellg();
  if (size <= 0) return 1;
  auto bytes = std::make_unique<std::vector<std::byte>>(
      static_cast<std::size_t>(size));
  in.seekg(0);
  in.read(reinterpret_cast<char*>(bytes->data()), size);
  if (!in) return 1;

  // Decode from Litematica to IR. DecodeFromFormat auto-detects gzip.
  auto schema_res = firc::DecodeFromFormat(
      fir::SourceFormat::kLitematica, std::move(bytes));
  if (!schema_res) {
    const fschema::ParseError& e = schema_res.error();
    // e.code, e.path, e.offset
    return 1;
  }
  fir::Schema& schema = *schema_res;

  if (schema.regions.empty()) return 1;
  fir::Region& region = schema.regions[0];

  // Select a 10x10x10 box in local coordinates.
  fl::BoxFilter box{0, 0, 0, 9, 9, 9};

  // MakeView materializes the region on demand.
  auto view_res = fl::MakeView(region, box, *schema.arena);
  if (!view_res) {
    const fschema::ParseError& e = view_res.error();
    return 1;
  }

  // Fill the selection with stone.
  auto fill_res = ed::Fill(*view_res, "minecraft:stone");
  if (!fill_res) {
    const fschema::ParseError& e = fill_res.error();
    return 1;
  }

  // Encode back to Litematica. Enable gzip compression for output.
  firc::EncodeOptions opts;
  opts.compress = true;

  auto encoded = firc::EncodeToFormat(
      fir::SourceFormat::kLitematica, schema, opts);
  if (!encoded) {
    const fschema::ParseError& e = encoded.error();
    return 1;
  }

  // Write the output file.
  std::ofstream out("output.litematic", std::ios::binary);
  if (!out) return 1;
  out.write(reinterpret_cast<const char*>(encoded->data()), encoded->size());
  if (!out) return 1;
}
```

## Convert between formats

`Convert` decodes and re-encodes in one call. It takes ownership of the byte  
buffer and returns the encoded bytes.

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

  firc::EncodeOptions opts;
  opts.target_version = 3;  // Sponge v3
  opts.multi_region =
      firc::EncodeOptions::MultiRegionStrategy::kMergeBoundingBox;
  opts.compress = true;

  auto result = firc::Convert(
      fir::SourceFormat::kLitematica,
      fir::SourceFormat::kSchem,
      std::move(bytes),
      opts);
  if (!result) {
    const fschema::ParseError& e = result.error();
    return 1;
  }

  std::ofstream out("output.schem", std::ios::binary);
  if (!out) return 1;
  out.write(reinterpret_cast<const char*>(result->data()), result->size());
  if (!out) return 1;
}
```

## Pitfalls

- Forgetting to register handlers. The error path is  
  `"FormatHandlerNotFound"`.

- Passing compressed bytes to the format-layer parsers  
  (`ParseLitematic`, `ParseSchematicFromBytes`). Those expect decompressed  
  bytes. The IR-layer `DecodeFromFormat` auto-detects gzip.

- Reading `Region::block_indices` without calling `EnsureMaterialized` or  
  going through `MakeView`. The buffer is empty until materialized.

- Holding a `std::string_view` from a palette entry after the `Schema` is  
  destroyed. All views point into the schema's owned buffer.

- Confusing `CompressorError` and `ParseError`. They are separate enums.  
  `Convert` wraps compression failures into `ParseError`.

## Next

- [Parsing](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/parsing.md)

- [Conversion](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/conversion.md)

- [Tool API Overview](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/tools/overview.md)
