# FastSchema

It's a super fast Minecraft schema tool library. Why it's so fast:

### Features

- **Decompression**: Integrated with `libdeflate` to save a few milliseconds of WallTime during `zlib-ng` decompression.

- **SIMD**: Built on Google Highway primitives with custom SIMD kernels targeting AVX2.

- **Multi-threading**: Parallelized via Intel oneTBB for multi-core scaling.

- **Toolchain**: C++23 standard (excluding C++20 Modules for build system compatibility).

But... How fast is it?

### Benchmark

> **No benchmark comparisons are provided here, as differences in output data structures and parsing semantics make direct performance comparisons potentially misleading.**

**Environment & Toolchain:** Intel Core i5-12400F (Alder Lake-S, 6C/12T), DDR4-3200 8GB×2. Windows 11 24H2(with huge pages), Clang-CL 22.1.3 -O2.

**Input Data:**

- `.litematic` Input Size: 279.642 MiB, Total Blocks: 260.297M, Palette Max: 465
- `.schem` Input Size: 248.778 MiB, Total Blocks: 260.297M, Palette Max: 466
- `.nbt` Input Size: 279.642 MiB

#### Pure Parsing & Unpacking

| Types                            | Wall Time | CPU Time | Iterations | Throughput    |
|:-------------------------------- |:--------- |:-------- |:---------- |:------------- |
| Pure NBT Parsing                 | 1.711 ms  | 1.680 ms | 1600       | 162.583 GiB/s |
| Litematic Parsing (ImmutableAPI) | 0.535 ms  | 0.522 ms | 5271       | 523.435 GiB/s |
| Litematic Unpack (MutableAPI)    | 53.4 ms   | 42.8 ms  | 69         | 6.372 GiB/s   |
| Schem Parsing (ImmutableAPI)     | 3.77 ms   | 3.72 ms  | 747        | 65.252 GiB/s  |
| Schem Unpack (MutableAPI)        | 32.1 ms   | 26.9 ms  | 138        | 9.002 GiB/s   |

#### Format Conversion

| Types                                  | Wall Time | CPU Time | Iterations | Throughput    |
|:-------------------------------------- |:--------- |:-------- |:---------- |:------------- |
| CrossConvert (Litematic -> Schem)      | 209 ms    | 201 ms   | 36         | 1.359 GiB/s   |
| RoundTripSame (Litematic -> Litematic) | 116 ms    | 113 ms   | 58         | 2.419 GiB/s   |
| CrossConvert (Schem -> Litematic)      | 357 ms    | 339 ms   | 20         | 733.723 MiB/s |
| RoundTripSame (Schem -> Schem)         | 112 ms    | 110 ms   | 61         | 2.216 GiB/s   |
| CrossConvertGzip (Litematic -> Schem)  | 620 ms    | 600 ms   | 8          | 466.374 MiB/s |
| CrossConvertGzip (Schem -> Litematic)  | 825 ms    | 803 ms   | 5          | 309.762 MiB/s |

> Gzip performance is mostly limited by Deflate itself. `libdeflate` is already highly optimized, and the results are close to the practical limit for this workload on the test machine.

### How to use

This library is still under development...

But it supports parsing or converting `.litematic`(v5 - v7) , `.schem`(v2 - v3) and NBT files now.

[You can get examples at here](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/examples).

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

#### Testing & Benchmarking

- [Google Test](https://github.com/google/googletest)

- [Google Benchmark](https://github.com/google/benchmark)
