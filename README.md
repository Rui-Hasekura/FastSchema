# FastSchema

It's a super fast Minecraft schema tool library. Why it's so fast:

### Features

- **Decompression**: Integrated with `libdeflate` to save a few milliseconds of WallTime during `zlib-ng` decompression.

- **SIMD**: Built on Google Highway primitives with custom SIMD kernels targeting AVX2.

- **Multi-threading**: Parallelized via Intel oneTBB for multi-core scaling.

But... How fast is it?

### Benchmark

> **No benchmark comparisons are provided here, as differences in output data structures and parsing semantics make direct performance comparisons potentially misleading.**

**Environment & Toolchain:** Intel Core i5-12400F (Alder Lake-S, 6C/12T), DDR4-3200 8GB×2. Windows 11 24H2(with huge pages), Clang-CL 22.1.3 -O2.

**Input Data:**

- `.litematic` Input Size: 279.642 MiB, Total Blocks: 260.297M, Palette Max: 465
- `.schem` Input Size: 248.778 MiB, Total Blocks: 260.297M, Palette Max: 466
- `.nbt` Input Size: 279.642 MiB

#### Pure Parsing & Unpacking

| Types                            | Wall Time | CPU Time | Iterations | Throughput   |
|:-------------------------------- |:--------- |:-------- |:---------- |:------------ |
| Pure NBT Parsing                 | 1.627 ms  | 1.594 ms | 1774       | 171.3GiB/s   |
| Litematic Parsing (ImmutableAPI) | 0.641 ms  | 0.622 ms | 4371       | 439.05GiB/s  |
| Litematic Unpack (MutableAPI)    | 45.5 ms   | 42.2 ms  | 64         | 6.45706GiB/s |
| Schem Parsing (ImmutableAPI)     | 2.23 ms   | 2.18 ms  | 1195       | 111.261GiB/s |
| Schem Unpack (MutableAPI)        | 25.3 ms   | 19.8 ms  | 119        | 12.2345GiB/s |

#### Format Conversion

| Types                                  | Wall Time | CPU Time | Iterations | Throughput    |
|:-------------------------------------- |:--------- |:-------- |:---------- |:------------- |
| CrossConvert (Litematic -> Schem)      | 206 ms    | 202 ms   | 34         | 1.354 GiB/s   |
| RoundTripSame (Litematic -> Litematic) | 119 ms    | 114 ms   | 61         | 2.396 GiB/s   |
| CrossConvert (Schem -> Litematic)      | 268 ms    | 261 ms   | 27         | 953.189 MiB/s |
| RoundTripSame (Schem -> Schem)         | 103 ms    | 102 ms   | 65         | 2.389 GiB/s   |
| CrossConvertGzip (Litematic -> Schem)  | 618 ms    | 598 ms   | 7          | 467.461 MiB/s |
| CrossConvertGzip (Schem -> Litematic)  | 738 ms    | 721 ms   | 6          | 344.876 MiB/s |

> Gzip performance is mostly limited by Deflate itself. `libdeflate` is already highly optimized, and the results are close to the practical limit for this workload on the test machine.

### Installation

##### Prerequisites

Before build it, ensure your environment meets the following requirements:

**CMake**: 3.28 or higher. [click me if need to update](https://cmake.org/download/)

**C++ Compiler**: Must support C++23. *Clang/GCC are recommended.*

- Recommended: **Clang** 17+, **GCC** 14+, MSVC 19.38+

- Compat: Clang 16+, GCC 13.1+, MSVC19.36+

##### Build & Install from Source

```bash
git clone https://github.com/Rui-Hasekura/FastSchema
cd FastSchema
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build
```

##### CMake `find_package`

```cmake
find_package(fschema REQUIRED)

add_executable(your_app main.cc)
target_link_libraries(your_app PRIVATE fschema::fschema)
```

> *Why not FetchContent? Well, we don't have any tags or releases yet...*

### How to use

This library is still under development.

It features a high-performance parser, converters(not because the implementation is bad or I lack confidence, but ideally avoid cross-converting if you can), and tools(quite a few, actually).

Documentation is work-in-progress. However, the API is fairly straightforward(at least I think), so you can explore it on your own for now.

[You can find examples here](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/examples).

### License

Distributed under the **Apache License 2.0**. See [LICENSE](https://github.com/Rui-Hasekura/FastSchema/blob/main/LICENSE) for details.

This project also includes a [NOTICE](https://github.com/Rui-Hasekura/FastSchema/blob/main/NOTICE) file that contains additional attribution notices as required by the license.

### Dependencies

#### Core Libraries

- [Google Highway](https://github.com/google/highway)

- [libdeflate](https://github.com/ebiggers/libdeflate)

- [Intel oneTBB](https://github.com/oneapi-src/oneTBB)

- [Google Abseil](https://github.com/abseil/abseil-cpp)

- [xxHash](https://github.com/cyan4973/xxhash)

- [pdqsort](https://github.com/orlp/pdqsort)

#### Testing & Benchmarking

- [Google Test](https://github.com/google/googletest)

- [Google Benchmark](https://github.com/google/benchmark)
