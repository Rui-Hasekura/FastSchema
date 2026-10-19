# RandomFilter

`fschema/filters/random_filter.h` defines `RandomFilter`.

```cpp
class RandomFilter {
 public:
  explicit RandomFilter(double probability,
                        std::uint64_t seed = 0x12345678ULL) noexcept;

  [[nodiscard]] bool operator()(LocalPos /*p*/,
                                std::uint16_t /*idx*/) noexcept;
};
```

Selects each block independently with probability `probability`.

- `probability <= 0.0` always returns false.

- `probability >= 1.0` always returns true.

- Values in between are converted to an integer threshold once, in the constructor. The inner loop is integer-only.

`seed` selects the sequence. Two instances with the same seed and the same traversal order select the same positions. The PRNG algorithm is an implementation detail.

`operator()` is not `const`. Each call advances the PRNG state, so the order of evaluation matters.

## RandomFilter does not satisfy Filter

`Filter` requires `const F&` to be callable. `RandomFilter::operator()` is non-const, so `RandomFilter` does not satisfy the concept and cannot be passed to `MakeView` directly.

Wrap it behind a const-callable lambda. A `shared_ptr` allows the state to mutate through the captured pointer:

```cpp
#include <memory>

#include "fschema/filters/random_filter.h"
#include "fschema/filters/view.h"

auto rng = std::make_shared<fl::RandomFilter>(0.1, /*seed=*/42);

auto filter = [rng](fl::LocalPos p, std::uint16_t idx) {
  return (*rng)(p, idx);
};

auto view_res = fl::MakeView(region, filter, *schematic.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

view_res->ForEach([](fl::LocalPos p, std::uint16_t pal) {
  // Selected blocks.
});
```

Holding `RandomFilter` by value and marking the lambda `mutable` also compiles, but a `mutable` lambda has a non-const `operator()` and does not satisfy `Filter` either. The `shared_ptr` form above satisfies the concept.

## Determinism

Given a fixed seed and a fixed traversal order, the selected set is reproducible. `ForEach` and `ForEachLinear` both walk the region in YZX order. Switching between them, or between parallel and serial execution, does not change the sequence for a given view.
