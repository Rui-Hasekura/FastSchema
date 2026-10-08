# BlockStateFilter

`fschema/filters/block_state_filter.h` defines `BlockStateCondition` and `BlockStateFilter`.

## BlockStateCondition

```cpp
struct BlockStateCondition {
  std::string_view key;
  std::string_view value;
};
```

A single key-value condition applied to a block state property, for example `{"facing", "north"}`.

## BlockStateFilter

```cpp
class BlockStateFilter {
 public:
  BlockStateFilter(const ir::Region& r,
                   std::vector<BlockStateCondition> conditions);

  [[nodiscard]] bool operator()(LocalPos /*p*/,
                                std::uint16_t palette_idx) const noexcept;

 private:
  std::vector<std::uint8_t> matched_;
};
```

Selects blocks whose palette entry matches all of the given conditions. An entry matches if, for every condition, the key is present and its value equals the condition's value.

Matching handles both property encodings:

- `PropertyEncoding::kString`: the raw properties are a `k1=v1,k2=v2` string. Each condition is matched against one `k=v` pair.
- `PropertyEncoding::kNbt`: the raw properties are a raw NBT Compound. Each condition is matched against a String-tagged field of that Compound.

Entries with `PropertyEncoding::kNone` or empty `raw_properties` never match.

The constructor builds a boolean LUT of size `palette.size()`. The inner loop is a palette-index lookup, so the per-block cost does not depend on how many conditions were passed.

The `key` and `value` string views in the conditions must outlive the filter.

## Example

```cpp
namespace fl = fschema::filters;

std::vector<fl::BlockStateCondition> conds = {
    {"facing", "north"},
    {"powered", "true"},
};

fl::BlockStateFilter powered_north{region, conds};

auto view_res = fl::MakeView(region, powered_north, arena);
if (!view_res) {
  // Handle ParseError.
  return;
}
view_res->for_each([](fl::LocalPos p, std::uint16_t palette_idx) {
  // ...
});
```

The conditions vector is taken by value. Empty conditions match every palette entry, including entries with no properties; combine with a name filter if that is not what you want.
