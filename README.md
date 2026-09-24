# FastSchema

It's a super fast Minecraft schema tool library. Why it's so fast:

### Features

- **Decompression**: Integrated with `libdeflate` to save a few milliseconds of WallTime during `zlib-ng` decompression.

- **SIMD**: Built on Google Highway primitives with custom SIMD kernels targeting AVX2.

- **Multi-threading**: Parallelized via Intel oneAPI TBB for multi-core scaling.

- **Toolchain**: C++23 standard (excluding C++20 Modules for build system compatibility).

But... How fast is it?

### Benchmark

**Environment & Toolchain:** Intel Core i5-12400F (Alder Lake-S, 6C12T), DDR4-3200 8GB×2. Windows 11 24H2, Clang-CL -O2.

#### Litematic Parsing

Full extraction of Litematic structure, including delayed SIMD unpacking of `BlockStates` into `uint16_t` block indices.

| Metric   | Wall Time | CPU Time | Iterations | Throughput  | Input Size  | Total Blocks |
| -------- | --------- | -------- | ---------- | ----------- | ----------- | ------------ |
| **Mean** | 37.2 ms   | 33.3 ms  | 69         | 8.204 GiB/s | 279.642 MiB | 260.297M     |

#### Pure NBT Parsing

Building the generic NBT tree from the decompressed memory buffer.

| Metric   | Wall Time | CPU Time | Iterations | Throughput   | Input Size  |
| -------- | --------- | -------- | ---------- | ------------ | ----------- |
| **Mean** | 1.760 ms  | 1.655 ms | 1378       | 164.96 GiB/s | 279.642 MiB |

> **Note on NBT Throughput:** The extraordinarily high throughput (164.96 GiB/s) in the Pure NBT benchmark is not a raw byte-level processing speed. In the current NBT parser implementation, large payloads like `ByteArray`, `IntArray`, and `LongArray` are handled via **zero-copy** `std::span`. The parser simply reads the length prefix and uses `ByteReader::advance()` to skip over the data block, retaining it as a raw byte span in the `NbtPayload`. For files like `.litematic` where the vast majority of the volume is a single `LongArray` (BlockStates), `ParseNbt` performs minimal actual byte-level work and tree construction, spending most of its time just advancing pointers. The heavy computation is deferred to the Litematic parsing stage, where the `LongArray` is actually unpacked into `uint16_t` indices via SIMD.*

#### Schem Parsing

Full extraction of Sponge Schematic structure, including delayed varint decoding of `BlockData` into `uint16_t` block indices.

| Metric   | Wall Time | CPU Time | Iterations | Throughput    | Input Size  | Total Blocks |
| -------- | --------- | -------- | ---------- | ------------- | ----------- | ------------ |
| **Mean** | 61.9 ms   | 40.0 ms  | 66         | 6.07224 GiB/s | 248.778 MiB | 260.297M     |

### How to use

This library is still under development...

But you can use it to parse `.litematic` , `.schem` and NBT files now.

#### 1. Parse Litematic File

For example:

```cpp
#include <iostream>
#include <memory>
#include <vector>

#include "fschema/base/decompression.h"
#include "fschema/base/error.h"
#include "fschema/litematic/parse.h"
#include "fschema/litematic/types.h"

namespace fb = fschema::base;
namespace fl = fschema::litematic;

int main() {
    // STEP 1: Decompression
    auto unpacked_bytes = fb::DecompressGzipFile("path/to/file.litematic");
    if (!unpacked_bytes) {
        std::cerr << "Decompress failed\n";
        return 1;
    }

    // STEP 2: Transfer ownership to ParseLitematic
    auto owner = std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
    auto result = fl::ParseLitematic(std::move(owner));

    if (!result) {
        const fschema::ParseError& err = result.error();
        std::cerr << "Parse failed: " << static_cast<int>(err.code)
                  << " at \"" << err.path << "\" (offset: " << err.offset << ")\n";
        return 1;
    }

    // Success — fully parsed litematic with zero-copy block indices
    const auto& litematic = *result;
    std::cout << "Name: " << litematic.metadata.name << "\n";
    std::cout << "Regions: " << litematic.regions.size() << "\n";
    std::cout << "Total blocks: " << litematic.metadata.total_blocks << "\n";

    for (std::size_t i = 0; i < litematic.regions.size(); ++i) {
        const auto& reg = litematic.regions[i];
        std::cout << "  Region [" << i << "]: "
                  << reg.block_indices.size() << " blocks, "
                  << reg.palette.size() << " palette entries\n";
    }
}
```

#### 2. Parse Raw NBT File

The NBT parser builds a generic tree using zero-copy spans for large arrays.

```cpp
#include <iostream>
#include <memory>
#include <span>
#include <variant>
#include <vector>

#include "fschema/base/decompression.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_parse.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/base/nbt_tag.h"

namespace fb = fschema::base;

int main() {
    // STEP 1: Decompression (gzip-compressed NBT files)
    auto unpacked_bytes = fb::DecompressGzipFile("path/to/file.nbt");
    if (!unpacked_bytes) {
        std::cerr << "Decompress failed\n";
        return 1;
    }

    // STEP 2: Wrap into a span and initialize the reader
    std::span<const std::byte> byte_span(*unpacked_bytes);
    fb::DecodeLimits limits;
    fb::ByteReader reader(byte_span, limits);

    // STEP 3: Parse the NBT tree
    auto result = fb::ParseNbt(reader);
    if (!result) {
        const fschema::ParseError& err = result.error();
        std::cerr << "Parse failed: " << static_cast<int>(err.code)
                  << " at \"" << err.path << "\" (offset: " << err.offset << ")\n";
        return 1;
    }

    std::cout << "Bytes consumed: " << reader.pos()
              << " / " << unpacked_bytes->size() << "\n";

    // Success — access the parsed NBT tree
    const auto& root_tag = *result;
    std::cout << "Root tag type: " << static_cast<int>(root_tag.type) << "\n";
    std::cout << "Root tag name: " << root_tag.name << "\n";

    if (root_tag.type == fb::TagType::Compound) {
        const auto* comp_ptr =
            std::get_if<std::unique_ptr<fb::NbtCompound>>(&root_tag.payload);
        if (comp_ptr && *comp_ptr) {
            const auto& compound = **comp_ptr;
            std::cout << "Children: " << compound.children.size() << "\n";
            for (const auto& child : compound.children) {
                std::cout << "  " << child.name
                          << " (type: " << static_cast<int>(child.type) << ")\n";
            }
        }
    }
}
```

#### 3. Parse Schem File

Parsing `.schem` files involves decompressing the data, setting up a reader, and calling the root parser. An example based on the benchmark code:

```cpp
#include <iostream>
#include <memory>
#include <span>
#include <vector>

#include "fschema/base/decompression.h"
#include "fschema/base/error.h"
#include "fschema/base/limits.h"
#include "fschema/base/nbt_reader.h"
#include "fschema/memory/arena.h"
#include "fschema/schem/internal/root.h"
#include "fschema/schem/types.h"

namespace fb = fschema::base;
namespace fm = fschema::memory;
namespace fsc = fschema::schem;

int main() {
    // STEP 1: Decompression
    auto unpacked_bytes = fb::DecompressGzipFile("path/to/file.schem");
    if (!unpacked_bytes) {
        std::cerr << "Decompress failed\n";
        return 1;
    }

    // STEP 2: Initialize Schematic and ByteReader
    fsc::Schematic schematic;
    schematic.arena = std::make_unique<fm::Arena>();
    schematic.owner = std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));

    fb::DecodeLimits limits;
    fb::ByteReader reader(
        std::span<const std::byte>(
            schematic.owner->data(),
            schematic.owner->size()),
        limits);

    // STEP 3: Parse schematic from reader
    auto result = fsc::internal::ParseRoot(reader, schematic);
    if (!result) {
        const fschema::ParseError& err = result.error();
        std::cerr << "Parse failed: " << static_cast<int>(err.code)
                  << " at \"" << err.path << "\" (offset: " << err.offset << ")\n";
        return 1;
    }

    // Success — access parsed schematic data
    std::cout << "Version: " << static_cast<int>(schematic.version) << "\n";
    std::cout << "Dimensions: " << schematic.width << " x "
              << schematic.height << " x " << schematic.length << "\n";
    std::cout << "Volume: " << fsc::VolumeOf(schematic) << "\n";
    std::cout << "Palette: " << schematic.palette.size() << "\n";
    std::cout << "Block Entities: " << schematic.block_entities.size() << "\n";
    std::cout << "Entities: " << schematic.entities.size() << "\n";

    if (!schematic.biome_indices.empty()) {
        std::cout << "Biome Palette: " << schematic.biome_palette.size() << "\n";
        std::cout << "Biome Volume: " << fsc::BiomeVolumeOf(schematic) << "\n";
    }
}
```

### License

Distributed under the **Apache License 2.0**. See [LICENSE](https://github.com/Rui-Hasekura/FastSchema/blob/main/LICENSE) for details.

This project also includes a [NOTICE](https://github.com/Rui-Hasekura/FastSchema/blob/main/NOTICE) file that contains additional attribution notices as required by the license.

### Dependencies

- [libdeflate](https://github.com/ebiggers/libdeflate)

- [Google Highway](https://github.com/google/highway)

- [Google Benchmark](https://github.com/google/benchmark)

- [Google Test](https://github.com/google/googletest)

- [Intel oneTBB](https://github.com/oneapi-src/oneTBB)
