# FastSchema

It's a super fast Minecraft schema tool library. Why it's so fast:

### Features

- **Decompression**: Integrated with `libdeflate` to save a few milliseconds of WallTime during `zlib-ng` unpack.

- **SIMD**: Built on Google Highway primitives with custom SIMD kernels targeting AVX2.

- **Multi-threading**: Parallelized via Intel oneAPI TBB for multi-core scaling.

- **Toolchain**: C++23 standard (excluding C++20 Modules for build system compatibility).



But... How fast is it?



### Benchmark

```cmake
# Benchmark Executable
add_executable(test
  # Replace the source file below with the one you want to benchmark/test
  bench/litematic_parse.cc
)
```

**Environment:** Benchmark averaged over 20 repetitions (`repeats:20`).

**Input Size:** 279.642 MiB / 260.297M Blocks.

| Metric     | Wall Time  | CPU Time   | Iterations | Throughput      | Input Size  | Total Blocks |
| ---------- | ---------- | ---------- | ---------- | --------------- | ----------- | ------------ |
| **Mean**   | 137 ms     | 128 ms     | 20         | 2.137 GiB/s     | 279.642 MiB | 260.297M     |
| **Median** | **136 ms** | **128 ms** | 20         | **2.140 GiB/s** | 279.642 MiB | 260.297M     |
| **StdDev** | 3.50 ms    | 5.29 ms    | 20         | 89.386 MiB/s    | 0           | 5.026        |
| **CV**     | 2.55 %     | 4.14 %     | 20         | 4.08 %          | 0.00 %      | 0.00 %       |

> The coefficient of variation ($CV \le 2.55\%$) demonstrates high stability across runs.

### How to use

This library is still under development...

But you can use it to parse `.litematic` files now.

For example:

```cpp
#include "parser/unpacker.h"
#include "parser/litematic/parse.h"
#include "parser/litematic/types.h"
// ...
// STEP1: Deconpression
auto unpacked_bytes = fschema::parser::UnpackLitematicFrom("path/to/file.litematic");
if (!unpacked_bytes) {
}
// ...
// STEP2: std::move to ParseLitematic
auto owner = std::make_unique<std::vector<std::byte>>(std::move(*unpacked_bytes));
auto result = fl::ParseLitematic(std::move(owner));

if (!result) {
    // Fail
    const fschema::parser::ParseError& err = result.error();
    std::cerr << "Parse failed: " << static_cast<int>(err.code) 
              << " at " << err.path << " (offset: " << err.offset << ")\n";
} else {
    // Success
    // Now you got parsed data.
    fschema::parser::litematic::Litematic litematic = std::move(*result);
}
```



### License

Distributed under the **Apache License 2.0**. See `LICENSE` for details.

 

### Dependencies

- [libdeflate](https://github.com/ebiggers/libdeflate)

- [Google Highway](https://github.com/google/highway)

- [Google Benchmark](https://github.com/google/benchmark)

- [Intel oneAPI TBB](https://github.com/oneapi-src/oneTBB)
