# BlockStateFilter

`fschema/filters/block_state_filter.h` defines `BlockStateCondition` and
`BlockStateFilter`.

## BlockStateCondition

```cpp
struct BlockStateCondition {
  std::string_view key;
  std::string_view value;
};
```

A single key-value condition on a block state property, for example `{"facing", "north"}`.

## BlockStateFilter

```cpp
class BlockStateFilter {
 public:
  BlockStateFilter(const ir::Region& r,
                   std::vector<BlockStateCondition> conditions);

  [[nodiscard]] bool operator()(LocalPos /*p*/,
                                std::uint16_t palette_idx) const noexcept;
};
```

Selects blocks whose palette entry matches all conditions. An entry matches if, for every condition, the key is present and its value equals the condition's value.

Entries with `PropertyEncoding::kNone` or empty `raw_properties` never match. Empty `conditions` matches every palette entry, including entries with no properties.

Both property encodings are handled:

- `PropertyEncoding::kString`: the raw properties are `k1=v1,k2=v2`. Each  
  condition is matched against one `k=v` pair.

- `PropertyEncoding::kNbt`: the raw properties are a raw NBT Compound. Each condition is matched against a String-tagged field.

The palette is scanned once at construction. Per-block evaluation is a constant-time lookup independent of the number of conditions.

`key` and `value` in the conditions must outlive the filter.

## Example

```cpp
#include "fschema/filters/block_state_filter.h"
#include "fschema/filters/view.h"

std::vector<fl::BlockStateCondition> conds = {
    {"facing", "north"},
    {"powered", "true"},
};

fl::BlockStateFilter powered_north{region, std::move(conds)};

auto view_res = fl::MakeView(region, powered_north, *schema.arena);
if (!view_res) {
  const fschema::ParseError& e = view_res.error();
  return;
}

view_res->ForEach([](fl::LocalPos p, std::uint16_t pal) {
  // Every selected palette entry has facing=north and powered=true.
});
```

Combine with a name filter when the property check alone is too broad:

```cpp
fl::BlockNameFilter button{"minecraft:oak_button", region};
auto selection = button && powered_north;
```


