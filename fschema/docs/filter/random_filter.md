# RandomFilter

`fschema/filters/random_filter.h` defines `RandomFilter`.

```cpp
class RandomFilter {
 public:
  explicit RandomFilter(double probability,
                        std::uint64_t seed = 0x12345678ULL) noexcept;

  [[nodiscard]] bool operator()(LocalPos /*p=*/,
                                std::uint16_t /*idx=*/) noexcept;

 private:
  /* PRNG state */
  std::uint64_t threshold_;
};
```

Selects each block independently with probability `probability`.

`probability <= 0.0` makes the filter always return false. `probability >= 1.0` makes it always return true. Values in between are converted to an integer threshold once, in the constructor; the inner loop is integer-only.

`seed` selects the sequence. Two `RandomFilter` instances with the same seed and the same traversal order produce the same selection. The PRNG algorithm is an implementation detail and is not part of the public contract.

`operator()` is not `const`. Each call advances the PRNG state, so two calls with the same `LocalPos` do not necessarily return the same value, and the order of evaluation matters.

## RandomFilter does not satisfy Filter

`Filter` requires a `const F&` to be callable. `RandomFilter::operator()` is non-const, so `RandomFilter` does not satisfy the concept and cannot be passed to `MakeView` directly.

To use it with a view, hold it behind something that makes the call const while still allowing the state to mutate:

```cpp
namespace fl = fschema::filters;

auto rng = std::make_shared<fl::RandomFilter>(0.1, /*seed=*/42);

// The lambda's operator() is const; *rng is mutable through the shared_ptr.
auto filter = [rng](fl::LocalPos p, std::uint16_t idx) {
  return (*rng)(p, idx);
};

auto view_res = fl::MakeView(region, filter, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});
```

Holding `RandomFilter` by value and marking the lambda `mutable` also compiles, but a `mutable` lambda is not const-callable and therefore does not satisfy `Filter` either. The `shared_ptr` form above does.

## Determinism

Given a fixed seed and a fixed traversal order, the selected set is reproducible. `for_each` walks blocks in YZX order, so two views over the same region and the same `RandomFilter` instance with the same seed select the same positions. Anything that changes traversal order, such as switching between `for_each` and `for_each_linear`, can change which blocks are selected even though the sequence itself is unchanged.
