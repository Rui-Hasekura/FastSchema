# FastSchema

C++23 Minecraft schematics tool library with SIMD-accelerated code paths.

For a more detailed understanding of the project, see README.md and `fschema/docs/`.

## C++

- Target **C++23**. Do not use C++20 Modules.
- Prefer modern features (`std::expected`, `std::span`, ranges, concepts, `constexpr`, `std::format`, etc.) over legacy idioms. Avoid raw `new`/`delete` and C-style casts except at C API boundaries.

## CMake

- Prefer modern CMake: targets, `target_link_libraries` with visibility keywords, `target_compile_features`, generator expressions, `FetchContent`. Avoid directory-scoped legacy commands (`include_directories`, `add_definitions`, etc.).

## Style

- Use the [Google C++ Style Guide](https://google.github.io/styleguide/cppguide.html) as a default, not as dogma.
- Follow Abseil's file organization conventions as a default for file layout — also not as dogma.
- Do not "fix" deviations already established in the codebase — e.g., C++23 instead of the guide's C++20, or `std::expected` instead of `absl::StatusOr`.
- When in doubt, match surrounding code over the guide.

## Comments and Docs

- Comments and documentation in **English only**.
- Describe what the code does. No marketing or promotional wording.

## Third-Party Dependencies

- Prefer **`FetchContent`**.
- For single-header libraries, vendor under `third_party/<name>/` with the upstream `LICENSE` and, if present, `NOTICE`.