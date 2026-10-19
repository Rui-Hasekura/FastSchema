# DataVersion Remap

The remap pass rewrites an `ir::Schematic` in place so its data semantics
match a target Minecraft DataVersion. `Convert` runs it between decode
and encode. `DecodeFromFormat` and `EncodeToFormat` do not.

The pass lives in `fschema/remap/`. The public API is
`fschema/remap/remap.h`.

## Entry points

```cpp
[[nodiscard]] ParseResult<RemapStats> RemapToDataVersion(
    ir::Schematic& schematic,
    std::int32_t target_data_version,
    const RemapOptions& options = {});

[[nodiscard]] ParseResult<void> MaybeRemapForTarget(
    ir::Schematic& schematic,
    ir::SourceFormat target_format,
    std::int32_t target_container_version,
    const RemapOptions& remap_options,
    RemapStats* stats_out);
```

`RemapToDataVersion` runs the pass at an explicit DataVersion. It rejects
`target_data_version <= 0` and targets whose container is below 2860
(Litematica v5). When `schematic.data_version` already equals the target,
it returns a `RemapStats` with `kNoOpAtTarget` set and leaves the schematic
unchanged.

`MaybeRemapForTarget` resolves the target DataVersion and runs
`RemapToDataVersion` only when the resolved value differs from
`schematic.data_version`. `Convert` calls this one.

## Target resolution

`MaybeRemapForTarget` reads `remap_options.target_data_version` first. A
non-zero value is used as-is. Otherwise the pass derives a target from
the container version via `ResolveDataVersionForContainer`:

| Target format | Target version | DataVersion | Note                  |
| ------------- | -------------- | ----------- | --------------------- |
| Litematica    | 5              | -1          | Rejected by policy    |
| Litematica    | 6              | 3463        | 1.20 anchor           |
| Litematica    | 7 or 0         | 0           | No remap (round-trip) |
| Sponge        | any            | 0           | No remap              |

A resolved value of `-1` makes `MaybeRemapForTarget` return
`UnsupportedVersion`. A resolved value of `0` means the caller did not
request a remap.

## Direction

The pass has two directions in its type, but only downgrade is
implemented.

```cpp
enum class RemapDirection : std::uint8_t {
  kDowngrade = 0,  // source dv > target dv
  kUpgrade = 1,    // source dv < target dv
};
```

`CountApplicableRules` returns zero for an upgrade request. The pass then
emits `kNoRulesCoverage` and returns the original schema unchanged.
`schematic.data_version` and `schematic.source_version` stay where they were.

## Rule window

Every rule row carries `anchor_dv`, the DataVersion at which the semantic
change landed. A rule is active when its anchor falls inside the
half-open window between source and target:

```cpp
[[nodiscard]] constexpr bool RuleActive(std::int32_t target_dv,
                                        std::int32_t source_dv,
                                        std::int32_t anchor_dv) noexcept {
  if (target_dv < source_dv) {
    return target_dv < anchor_dv && anchor_dv <= source_dv;
  }
  if (source_dv < target_dv) {
    return source_dv < anchor_dv && anchor_dv <= target_dv;
  }
  return false;
}
```

A source equal to the target has no active rules.

## Families

The pass is organised into families. Each family has its own gate on the
target window and its own pre-scan patterns.

| Family       | Data touched                                                                                    | Effect                                                                                                            |
| ------------ | ----------------------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| Palette      | `Region::palette`                                                                               | `BlockState::name` renamed through the id-rename table                                                            |
| Block entity | `BlockEntity::raw_nbt`                                                                          | Root fields renamed, `components` unwrapped, BE id synthesized or renamed, embedded items rewritten               |
| Entity       | `Entity::raw_nbt`                                                                               | Root fields renamed, `equipment` / `drop_chances` split into target lists, `Attributes` rewritten, boat ids split |
| Item         | `raw_nbt` under `Entity::Item`, `Items`, `Inventory`, `Book`, `RecordItem`, block entity `item` | Component form rewritten to the target tag form                                                                   |
| Text         | `raw_nbt` fields named `CustomName`, `messages`, `front_text`, `back_text`, `title`, `pages`    | JSON-form strings written into the field                                                                          |
| Attribute    | `Attributes` lists and `minecraft:attribute_modifiers`                                          | Entries renamed through the attribute table, ids converted to IntArray UUIDs                                      |

The families run palette, block entities, entities, inside
`RemapToDataVersion`. Item, text, and attribute rewriting runs inside the
entity and block entity walkers, dispatched by field name and component
name.

Rule tables live in `fschema/remap/internal/tables/`. Lookup is binary
search over sorted inline arrays:

- `component_map.inc`
- `block_id_renames.inc`
- `attribute_renames.inc`
- `potion_effect_ids.inc`
- `banner_colors.inc`
- `banner_patterns.inc`
- `decoration_types.inc`
- `beacon_effects.inc`
- `tile_entity_id_guesses.inc`

A consteval guard in `fschema/remap/internal/rule.h` checks the sort
order of the component, id-rename, attribute, and potion-effect tables at
compile time. A `lower_bound` on an unsorted table would compile and
silently miss rows; the guard prevents that.

## Pre-scan

Entity and block entity NBT is not parsed unless a rule is likely to
apply. `RemapBlockEntity` and `RemapEntity` first run `ContainsPattern`
over `raw_nbt` for a fixed list of byte patterns. Each family contributes
patterns:

- items: `components`, `Items`, `Inventory`, `item`, `RecordItem`, `Book`
- text: `CustomName`, `messages`, `front_text`, `back_text`
- block entity structure: `profile`, `patterns`, `bees`
- entity structure: `equipment`, `drop_chances`
- attributes: `Attributes`
- boat split: `_boat`, `_raft`

A root with no hit keeps its `raw_nbt` span and is not parsed.

## Materialization and persistence

A hit root goes through:

1. `MaterializeCompound(raw_nbt)` into a `base::NbtCompound`.
2. The family rewrites.
3. `SerializeCompoundBody` back into bytes.
4. `RemapContext::PersistBytes` copies the bytes into `Schematic::arena`.

The new span replaces `Entity::raw_nbt` or `BlockEntity::raw_nbt`. It
stays valid for the lifetime of the schematic, like every other arena
allocation. `PersistBytes` returns an empty span when the arena
allocation fails; the caller keeps the original bytes in that case.

This flow does not touch `Region::block_indices` or `Region::lazy_source`.
The pass never calls `EnsureMaterialized`. See
[Materialization](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/materialization.md).

## Interaction with passthrough

The palette family rewrites block ids in `region.palette` and nothing
else. A region that would have taken the Litematica or Sponge passthrough
path before remap still takes it after. The rename table does not change
`palette.size()` and does not change whether `palette[0]` is an air
variant, so `PaletteBpb` and `air_at_zero` remain valid. See
[Passthrough Encoding](https://github.com/Rui-Hasekura/FastSchema/blob/main/fschema/docs/advanced/passthrough.md).

Block entity and entity spans are serialized from IR on every encode, so
their remap has no passthrough interaction.

## Options

```cpp
struct RemapOptions {
  std::int32_t target_data_version = 0;
  std::uint32_t max_diagnostics = 64;

  enum class UnmappedComponentPolicy : std::uint8_t {
    kDrop = 0,
    kMergeIntoTag = 1,
  };
  UnmappedComponentPolicy unmapped_components =
      UnmappedComponentPolicy::kDrop;
};
```

`target_data_version` is an override. `0` derives from the target
container.

`max_diagnostics` caps the retained `Diagnostic` entries. Events beyond
the cap increment `RemapStats::dropped_diagnostics` and do not allocate.

`unmapped_components` controls what happens to a component the pass
knows but has no tag-form equivalent for. `kDrop` removes it.
`kMergeIntoTag` keeps its body under the item's `tag` compound. The
default is `kDrop`.

## Stats and diagnostics

`RemapStats` carries the direction, source and target DataVersions,
counters, a watchlist, the retained diagnostics, and the active rule
count.

```cpp
struct RemapStats {
  RemapDirection direction = RemapDirection::kDowngrade;
  std::int32_t source_data_version = 0;
  std::int32_t target_data_version = 0;

  RemapCounters counters{};
  Watchlist watchlist{};

  absl::InlinedVector<Diagnostic, 8> diagnostics;
  std::uint32_t dropped_diagnostics = 0;

  std::uint32_t active_rules = 0;
};
```

`Diagnostic` is `{code, rule_id, path}`. `code` is a `Diag` enum value.
`rule_id` is zero for table-less events. `path` is an IR-relative node
path such as `Regions[0].Entities[3].Items[0]`.

`Watchlist` is a fixed 65536-bit bitmap. `Set(code)` records a code;
`Test(code)` checks it. It records that a code was emitted at least once,
independent of whether the entry survived the `max_diagnostics` cap. Use
it to distinguish "this code never fired" from "it fired but the entry
was dropped".

Diagnostic codes are grouped by family:

- `0x0xx` info: `kNoRulesCoverage`, `kNoOpAtTarget`.
- `0x2xx` items and components: component-to-tag mappings, drops, count
  and level clamps, hide-flag folding, container flattening, predicate
  downgrades, effect drops, book and firework fallbacks.
- `0x3xx` text: `kTextDowngraded`, `kTextUnsupportedShape`.
- `0x4xx` attributes: renames, no-target drops, malformed entries, UUID
  hashing, slot broadening.
- `0x5xx` entities: equipment and drop-chance rewrites, saddle rewrites,
  field drops, component-kept, boat split.
- `0x6xx` block entities: explicit-slot preservation, field drops, id
  guess and id-fail.
- `0x7xx` palette: id rename, no-target.
- `0x8xx` fallback: generic passthrough.

The full enum is in `fschema/remap/remap.h`. `RemapCounters` tracks
per-event counts; `items_rewritten`, `components_mapped`,
`components_dropped`, `attribute_renames`, `attribute_no_target`,
`entity_equipment_rewritten`, `be_ids_guessed`, `be_ids_unguessed`,
`texts_downgraded`, `te_wrappers_unwrapped`, `roots_scanned`, and
`roots_materialized` are the fields most callers check.

## Failures

`RemapToDataVersion` and `MaybeRemapForTarget` return
`ParseResult<...>`. Hard failures are:

- a target DataVersion `<= 0`,
- a target container below 2860,
- a container-policy rejection (`-1`),
- IR corruption during the walk (`MaterializeCompound` failure on a hit
  root).

Everything else is a diagnostic. Diagnostic events do not fail the
`Convert` call. `Convert` returns bytes even when the pass emitted
diagnostics. Callers that need the events read the `remap_stats`
out-param.

## Reading the outcome

After a `Convert` with `opts.remap_stats` set:

```cpp
if (!stats.diagnostics.empty()) {
  for (const auto& d : stats.diagnostics) {
    // d.code, d.rule_id, d.path
  }
}
if (stats.watchlist.Test(static_cast<std::uint32_t>(
        fschema::remap::Diag::kItemCountClamped))) {
  // at least one count was clamped, whether or not it is in diagnostics
}
if (stats.active_rules == 0) {
  // no rules covered the window; the data was not touched
}
```

`active_rules` counts rule rows whose anchor is in the target window.
`0` means the data was not touched.
