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

### How to use

This library is still under development...

But you can use it to parse `.litematic` and NBT files now.

#### 1. Parse Litematic File

For example:

```cpp
#include <iostream>
#include <memory>
#include <vector>

#include "parser/error.h"
#include "parser/unpacker.h"
#include "parser/litematic/parse.h"
#include "parser/litematic/types.h"

namespace fp = fschema::parser;
namespace fl = fschema::parser::litematic;

int main() {
    // STEP 1: Decompression
    auto unpacked_bytes = fp::DecompressGzipFile("path/to/file.litematic");
    if (!unpacked_bytes) {
        std::cerr << "Decompress failed: " 
                  << fp::ToString(unpacked_bytes.error()) << "\n";
        return 1;
    }

    // STEP 2: Transfer ownership to ParseLitematic
    auto owner = std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
    auto result = fl::ParseLitematic(std::move(owner));

    if (!result) {
        // Fail
        const fp::ParseError& err = result.error();
        std::cerr << "Parse failed: " << static_cast<int>(err.code) 
                  << " at \"" << err.path << "\" (offset: " << err.offset << ")\n";
        return 1;
    }

    // Success
    // Now you got the fully parsed data.
    fl::Litematic litematic = std::move(*result);

    // Example: Access metadata
    std::cout << "Litematic Name: " << litematic.metadata.name << "\n";
    std::cout << "Regions: " << litematic.regions.size() << "\n";

    return 0;
}
```

#### 2. Parse Raw NBT File

The NBT parser builds a generic tree using zero-copy spans for large arrays.

```cpp
#include <iostream>
#include <memory>
#include <variant>
#include <vector>

#include "parser/error.h"
#include "parser/limits.h"
#include "parser/nbt/parse.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/tag.h"
#include "parser/nbt_tree.h"
#include "parser/unpacker.h"

namespace fp = fschema::parser;
namespace nbt = fschema::parser::nbt;

int main() {
    // STEP 1: Decompression (Assuming a gzip-compressed NBT file)
    auto unpacked_bytes = fp::DecompressGzipFile("path/to/file.nbt");
    if (!unpacked_bytes) {
        std::cerr << "Decompress failed: " 
                  << fp::ToString(unpacked_bytes.error()) << "\n";
        return 1;
    }

    // STEP 2: Wrap into a span and initialize the reader
    std::span<const std::byte> byte_span(*unpacked_bytes);
    fp::DecodeLimits limits; // Default limits
    nbt::ByteReader reader(byte_span, limits);

    // STEP 3: Parse the NBT tree
    auto result = nbt::ParseNbt(reader);
    if (!result) {
        const fp::ParseError& err = result.error();
        std::cerr << "Parse failed: " << static_cast<int>(err.code) 
                  << " at \"" << err.path << "\" (offset: " << err.offset << ")\n";
        return 1;
    }

    // Success
    const nbt::NbtTag& root_tag = *result;
    std::cout << "Root tag type: " << static_cast<int>(root_tag.type) << "\n";
    std::cout << "Root tag name: " << root_tag.name << "\n";

    // Example: Access children if it's a Compound
    if (root_tag.type == nbt::TagType::Compound) {
        const auto& comp_ptr = std::get<std::unique_ptr<nbt::NbtCompound>>(root_tag.payload);
        const auto& compound = *comp_ptr;
        std::cout << "Children count: " << compound.children.size() << "\n";

        for (const auto& child : compound.children) {
            std::cout << " - " << child.name 
                      << " (Type: " << static_cast<int>(child.type) << ")\n";
        }
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

- [Intel oneAPI TBB](https://github.com/oneapi-src/oneTBB)
