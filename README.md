# FastSchema

It's a super fast Minecraft schema tool library. Why it's so fast:

### Features

- **Decompression**: Integrated with `libdeflate` to save a few milliseconds of WallTime during `zlib-ng` decompression.

- **SIMD**: Built on Google Highway primitives with custom SIMD kernels targeting AVX2.

- **Multi-threading**: Parallelized via Intel oneTBB for multi-core scaling.

- **Toolchain**: C++23 standard (excluding C++20 Modules for build system compatibility).

But... How fast is it?

### Benchmark

**Environment & Toolchain:** Intel Core i5-12400F (Alder Lake-S, 6C12T), DDR4-3200 8GB×2. Windows 11 24H2, Clang-CL 22.1.3 -O2.

#### Litematic Parsing

Full extraction of Litematic structure, including delayed SIMD unpacking of `BlockStates` into `uint16_t` block indices.

| Metric   | Wall Time | CPU Time | Iterations | Throughput  | Input Size  | Total Blocks |
| -------- | --------- | -------- | ---------- | ----------- | ----------- | ------------ |
| **Mean** | 44.3 ms   | 31.5 ms  | 105        | 8.656 GiB/s | 279.642 MiB | 260.297M     |

#### Pure NBT Parsing

Building the generic NBT tree from the decompressed memory buffer.

| Metric   | Wall Time | CPU Time | Iterations | Throughput    | Input Size  |
| -------- | --------- | -------- | ---------- | ------------- | ----------- |
| **Mean** | 1.697 ms  | 1.638 ms | 1707       | 166.672 GiB/s | 279.642 MiB |

> **Note on NBT Throughput:** The extraordinarily high throughput (166.672 GiB/s) in the Pure NBT benchmark is not a raw byte-level processing speed. In the current NBT parser implementation, large payloads like `ByteArray`, `IntArray`, and `LongArray` are handled via **zero-copy** `std::span`. The parser simply reads the length prefix and uses `ByteReader::advance()` to skip over the data block, retaining it as a raw byte span in the `NbtPayload`. For files like `.litematic` where the vast majority of the volume is a single `LongArray` (BlockStates), `ParseNbt` performs minimal actual byte-level work and tree construction, spending most of its time just advancing pointers. The heavy computation is deferred to the Litematic parsing stage, where the `LongArray` is actually unpacked into `uint16_t` indices via SIMD.

#### Schem Parsing

Full extraction of Sponge Schematic structure, including delayed varint decoding of `BlockData` into `uint16_t` block indices. Utilizes an optimized SIMD scanning kernel that detects and skips large continuous Air (`0x00`) blocks to bypass memory write bottlenecks.

| Metric   | Wall Time | CPU Time | Iterations | Throughput    | Input Size  | Total Blocks |
| -------- | --------- | -------- | ---------- | ------------- | ----------- | ------------ |
| **Mean** | 28.0 ms   | 22.8 ms  | 100        | 10.6497 GiB/s | 248.778 MiB | 260.297M     |

#### Format Conversion (CrossConvert & RoundTrip)

| Benchmark Task    | Wall Time | CPU Time | Iterations | Throughput    | Input Size  |
|:----------------- |:--------- |:-------- |:---------- |:------------- |:----------- |
| **CrossConvert**  | 2055 ms   | 2031 ms  | 1          | 179.603 MiB/s | 364.819 MiB |
| **RoundTripSame** | 2782 ms   | 2750 ms  | 16         | 132.661 MiB/s | 364.819 MiB |

> Todo: Boost it. We supported just now.

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
