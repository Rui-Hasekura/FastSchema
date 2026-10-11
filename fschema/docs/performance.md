# Performance

This page collects the shared performance characteristics of editors and
inspectors. It does not affect any API contract; results are identical
whether or not the parallel or SIMD paths are taken.

The details below may change with the implementation. Only the observable
behaviour described in the individual editor and inspector pages is part
of the public contract.

## Parallelism

Operations that scan a region are parallelized when the region has at
least 256K blocks (`kParallelThreshold = 1 << 18`). The work is split
across the Y-axis and dispatched with TBB (`tbb::parallel_for`,
`tbb::parallel_reduce`, or `tbb::concurrent_vector` depending on the
operation).

Parallelization is transparent: results and ordering do not depend on the
number of threads. Any operation whose output order is observable (for
example `Extract` returning a compact region, or `ListMaterial` returning
entries in palette order) produces the same result serially and in
parallel.

## SIMD acceleration (AVX2)

The AVX2 path is an implementation detail and may be replaced or removed
without notice.

On AVX2 targets, the inner scan loop processes 16 blocks at a time:

- Palette indices are loaded with `_mm256_loadu_si256`.
- Out-of-range indices are clamped to a padding slot with
  `_mm256_min_epu16`, so LUT lookups are always in bounds.
- LUT entries are fetched with `_mm256_i32gather_epi32`.
- Match masks are extracted with `_mm256_movemask_epi8` and counted with
  `std::popcount`.

When a chunk is entirely matched, `fill` and `replace` use
`_mm256_stream_si256` to bypass the cache and avoid write-allocate
overhead. The row prefix is handled separately so the streaming stores
stay aligned.

None of these details are part of the public contract. The observable
behaviour is the same on AVX2 and on the portable fallback.

## Threshold

The parallel threshold is `1 << 18` (262144 blocks). It is a compile-time
constant in each header (`kParallelThreshold`). Below it, the scan runs
serially; above it, it is parallelized.

The threshold value is also an implementation detail; do not depend on
it in caller code.