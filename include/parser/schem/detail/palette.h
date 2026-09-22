#ifndef FSCHEMA_PARSER_SCHEM_DETAIL_PALETTE_H_
#define FSCHEMA_PARSER_SCHEM_DETAIL_PALETTE_H_

#include <expected>
#include <vector>

#include "parser/error.h"
#include "parser/nbt/reader.h"
#include "parser/schem/types.h"

namespace fschema::parser::schem::detail {

  // Parses a block palette Compound.
  // Keys are blockstate strings, values are Int indices.
  //   { "minecraft:air": 0, "minecraft:stone_button[face=floor]": 1, ... }
  // Indices may be non-contiguous; gaps are left as default BlockState.
  // Blockstate strings are parsed into name + properties.
  [[nodiscard]] ParseResult<void> ParseBlockPalette(
    nbt::ByteReader& reader,
    std::vector<BlockState>& palette);

  // Parses a biome palette Compound.
  // Keys are biome resource location strings, values are Int indices.
  [[nodiscard]] ParseResult<void> ParseBiomePalette(
    nbt::ByteReader& reader,
    std::vector<std::string_view>& palette);

}  // namespace fschema::parser::schem::detail

#endif  // FSCHEMA_PARSER_SCHEM_DETAIL_PALETTE_H_