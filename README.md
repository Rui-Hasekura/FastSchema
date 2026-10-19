# FastSchema

It's a super fast Minecraft schematic tool library. Why it's so fast:

### 1. Features

- **Decompression**: Utilizes `libdeflate` for decompression.

- **SIMD**: Built on `Google Highway` primitives with custom SIMD kernels targeting AVX2.

- **Multi-threading**: Parallelized via `Intel oneTBB` for multi-core scaling.

- **Cross-version data remapping**: Optional DataVersion downgrade pass
  (1.20.5+ item components → 1.20.1 tags, 1.21.5 entity structure reorg,
  block / attribute id rollback), driven by version-windowed rule tables.

But... How fast is it?

### 2. Benchmark

> **No benchmark comparisons are provided here, as differences in output data structures and parsing semantics make direct performance comparisons potentially misleading.**

**Environment & Toolchain:** Intel Core i5-12400F (Alder Lake-S, 6C/12T), DDR4-3200 8GB×2. Windows 11 24H2(with large pages), Clang-CL 22.1.3 -O2.

**Input Data:**

The test sample was actually the Valkyrie Space Station 3.
You can download it from [here](https://2b2t.miraheze.org/wiki/Space_Valkyria_3_V2).

- `.litematic` Input Size: 279.642 MiB, Total Blocks: 260.297M, Palette Max: 465
- `.schem` Input Size: 248.778 MiB, Total Blocks: 260.297M, Palette Max: 466
- `.nbt` Input Size: 279.642 MiB

#### 2.1 Pure Parsing & Unpacking

| Types                             | Wall Time | CPU Time | Iterations | Throughput   |
|:--------------------------------- |:--------- |:-------- |:---------- |:------------ |
| Pure NBT Parsing                  | 1.627 ms  | 1.594 ms | 1774       | 171.3GiB/s   |
| Litematica Parsing (ImmutableAPI) | 0.641 ms  | 0.622 ms | 4371       | 439.05GiB/s  |
| Litematica Unpack (MutableAPI)    | 45.5 ms   | 42.2 ms  | 64         | 6.45706GiB/s |
| Sponge Parsing (ImmutableAPI)     | 2.23 ms   | 2.18 ms  | 1195       | 111.261GiB/s |
| Sponge Unpack (MutableAPI)        | 25.3 ms   | 19.8 ms  | 119        | 12.2345GiB/s |

#### 2.2 Format Conversion

| Types                                    | Wall Time | CPU Time | Iterations | Throughput   |
|:---------------------------------------- |:--------- |:-------- |:---------- |:------------ |
| CrossConvert (Litematica -> Sponge)      | 212 ms    | 202 ms   | 34         | 1.35054 Gi/s |
| RoundTripSame (Litematica -> Litematica) | 118 ms    | 114 ms   | 62         | 2.39208 Gi/s |
| CrossConvert (Sponge -> Litematica)      | 547 ms    | 523 ms   | 14         | 475.277 Mi/s |
| RoundTripSame (Sponge -> Sponge)         | 105 ms    | 103 ms   | 66         | 2.36454 Gi/s |
| CrossConvertGzip (Litematica -> Sponge)  | 610 ms    | 587 ms   | 7          | 476.348 Mi/s |
| CrossConvertGzip (Sponge -> Litematica)  | 1018 ms   | 1008 ms  | 4          | 246.849 Mi/s |

> Gzip performance is mostly limited by Deflate itself. `libdeflate` is already highly optimized, and the results are close to the practical limit for this workload on the test machine.

> Conversion benchmarks use the default remap policy. With
> `target_version = 0`, `Convert` resolves to no remap for the listed
> targets, so the numbers cover decode + encode only. Forcing a remap
> (`remap.target_data_version = 3463`) adds a pass proportional to the
> entity and block-entity NBT in the file; it is not included above.

### 3. Installation & Integration

**Prerequisites**

Before building or integrating, ensure your environment meets the following requirements:

- **CMake**: 3.28 or higher. ([Update](https://cmake.org/download/))
- **C++ Compiler**: Must support C++23.
  - Recommended: **Clang** 17+, **GCC** 14+, MSVC 19.38+
  - Compatible: Clang 16+, GCC 13.1+, MSVC 19.36+

---

#### 3.1 Integrate via CMake `FetchContent` (Recommended)

You can embed FastSchema into your CMake project without installing it globally:

```cmake
include(FetchContent)

FetchContent_Declare(
 FastSchema
 GIT_REPOSITORY https://github.com/Rui-Hasekura/FastSchema.git
 GIT_TAG v2.0.0 # Or you can replace with the latest tag
)
```

##### Optional: Disable tests and benchmarks in parent project

```cmake
set(FASTSCHEMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(FASTSCHEMA_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(FastSchema)

add_executable(your_app main.cc)
target_link_libraries(your_app PRIVATE fschema::fschema)
```

#### 3.2 Build & Install from Source

If you perfer to install FastSchema system-wide or generate packages for distribution:

```bash
git clone https://github.com/Rui-Hasekura/FastSchema
cd FastSchema

cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
cmake --install build
```

#### Use via CMake `find_package`

After installing, locate FastSchema in your project's `CMakeLists.txt`:

```cmake
find_package(fschema 2.0.0 CONFIG REQUIRED)
# ...
target_link_libraries(your_app PRIVATE fschema::fschema)
```

### 4. How to use

It features a high-performance parser, converters(not because the implementation is bad or I lack confidence, but ideally avoid cross-converting if you can), and tools(quite a few, actually).

About remap:

- Remap is downgrade-only. `target_data_version` defaults to 3463 (JE 1.20)
  when the Litematica target version is 6, and is unset (no remap) for
  Litematica v7 and for any Sponge target.

- `target_version = 5` requires an explicit `remap.target_data_version >= 2860`;
  otherwise it is rejected by policy. The policy floor means the data lands
  at v6-or-later semantics even though the container version field is 5.

[Documentation](https://github.com/Rui-Hasekura/FastSchema/tree/main/fschema/docs/) is now available.

### 5. License

Distributed under the **Apache License 2.0**. See [LICENSE](https://github.com/Rui-Hasekura/FastSchema/blob/main/LICENSE) for details.

This project also includes a [NOTICE](https://github.com/Rui-Hasekura/FastSchema/blob/main/NOTICE) file that contains additional attribution notices as required by the license.

### 6. Dependencies

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
