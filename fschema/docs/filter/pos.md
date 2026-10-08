# Pos

`fschema/filters/pos.h` defines `LocalPos`, the `Axis` and `Cmp` enums,
and `LocalPosHash`.

`LinearIndex` is defined in `fschema/filters/view.h`; it maps a `LocalPos`
to an index into `Region::block_indices`. See below.

## LocalPos

A block coordinate relative to a `Region` origin.

```cpp
struct LocalPos {
  std::int32_t x = 0;
  std::int32_t y = 0;
  std::int32_t z = 0;

  friend bool operator==(const LocalPos&, const LocalPos&) = default;
};
```

For a valid position inside a region with bounds `B`,
`0 <= x < B.size[0]`, `0 <= y < B.size[1]`, and
`0 <= z < B.size[2]`. Filters and editors receiving a `LocalPos`
generally assume it is in-bounds. `IsAirAt` and the other
`filters::internal` neighbor helpers are the exception: they treat
out-of-bounds coordinates as air.

The defaulted `operator==` is required to use `LocalPos` as a key in
`absl::flat_hash_set` and `absl::flat_hash_map`.

### LocalPosHash

```cpp
struct LocalPosHash {
  [[nodiscard]] std::size_t operator()(const LocalPos& p) const noexcept;
};
```

`LocalPosHash` is the hash functor for `LocalPos`. Use it when storing
`LocalPos` in `absl::flat_hash_set` or `absl::flat_hash_map`, as
`NbtFilter` does.

## Axis

```cpp
enum class Axis : std::uint8_t { X = 0, Y = 1, Z = 2 };
```

The values `0`, `1`, `2` match the array indices of `BoundingBox::size`
and `BoundingBox::origin`, so generic code can do
`bounds.size[static_cast<int>(axis)]` without a `switch`.

## Cmp

```cpp
enum class Cmp : std::uint8_t {
  Lt,  // <
  Le,  // <=
  Ge,  // >=
  Gt,  // >
  Eq,  // ==
  Ne,  // !=
};
```

`Cmp` is the comparison operator for spatial filters such as
`AxisFilter`. `WorldAxis` converts a world-space threshold into the
local threshold used by `AxisFilter`.

## LinearIndex

Defined in `fschema/filters/view.h`.

```cpp
[[nodiscard]] inline std::uint64_t LinearIndex(
    LocalPos p, const ir::BoundingBox& b) noexcept;
```

A `Region` stores block data as a flat 1D
`UnInitBuffer<std::uint16_t>`. `LinearIndex` maps a 3D `LocalPos` to the
corresponding 1D subscript.

Minecraft Java Edition, Litematica, and Sponge Schematic all use YZX
order (Y major, Z middle, X minor):

`idx = y * (size_x * size_z) + z * size_x + x`

Do not hand-write `y * width * depth + z * width + x`; use
`filters::LinearIndex` to avoid dimension-order mistakes.
