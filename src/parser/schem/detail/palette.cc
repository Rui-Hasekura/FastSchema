#include "parser/schem/detail/palette.h"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string_view>
#include <utility>
#include <vector>

#include "parser/error.h"
#include "parser/nbt/reader.h"
#include "parser/nbt/skip.h"
#include "parser/nbt/tag.h"
#include "parser/schem/block_state_string.h"
#include "parser/schem/types.h"

namespace fschema::parser::schem::detail {

  struct PaletteEntry {
    std::string_view key;
    std::int32_t index;
  };

  [[nodiscard]] ParseResult<void> ParseBlockPalette(
    nbt::ByteReader& reader,
    std::vector<BlockState>& palette) {
    reader.push_depth();

    std::vector<PaletteEntry> entries;
    entries.reserve(64);
    std::int32_t max_index = -1;

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      if (*tag_result != nbt::TagType::Int) {
        auto skip_result = nbt::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        continue;
      }

      auto value = reader.Read<std::int32_t>();
      if (!value) {
        reader.pop_depth();
        return std::unexpected(value.error());
      }

      if (*value < 0) [[unlikely]] {
        reader.pop_depth();
        return std::unexpected(
          reader.Error(ParseError::Code::NegativeLength));
      }

      entries.push_back({ name, *value });
      if (*value > max_index) {
        max_index = *value;
      }
    }

    reader.pop_depth();

    if (max_index < 0) {
      palette.clear();
      return {};
    }

    if (static_cast<std::size_t>(max_index + 1) >
      reader.limits().max_palette_size) [[unlikely]] {
      return std::unexpected(ParseError{
          ParseError::Code::OversizedPayload,
          "Palette", 0 });
    }

    palette.resize(static_cast<std::size_t>(max_index + 1));
    for (const auto& entry : entries) {
      auto& bs = palette[static_cast<std::size_t>(entry.index)];
      ParseBlockStateString(entry.key, bs);
    }

    return {};
  }

  [[nodiscard]] ParseResult<void> ParseBiomePalette(
    nbt::ByteReader& reader,
    std::vector<std::string_view>& palette) {
    reader.push_depth();

    std::vector<PaletteEntry> entries;
    entries.reserve(32);
    std::int32_t max_index = -1;

    for (;;) {
      std::string_view name;
      auto tag_result = reader.ReadCompoundEntryHeaderView(name);
      if (!tag_result) {
        reader.pop_depth();
        return std::unexpected(tag_result.error());
      }
      if (*tag_result == nbt::TagType::End) {
        break;
      }

      if (*tag_result != nbt::TagType::Int) {
        auto skip_result = nbt::SkipPayload(reader, *tag_result);
        if (!skip_result) {
          reader.pop_depth();
          return std::unexpected(skip_result.error());
        }
        continue;
      }

      auto value = reader.Read<std::int32_t>();
      if (!value) {
        reader.pop_depth();
        return std::unexpected(value.error());
      }

      if (*value < 0) [[unlikely]] {
        reader.pop_depth();
        return std::unexpected(
          reader.Error(ParseError::Code::NegativeLength));
      }

      entries.push_back({ name, *value });
      if (*value > max_index) {
        max_index = *value;
      }
    }

    reader.pop_depth();

    if (max_index < 0) {
      palette.clear();
      return {};
    }

    if (static_cast<std::size_t>(max_index + 1) >
      reader.limits().max_palette_size) [[unlikely]] {
      return std::unexpected(ParseError{
          ParseError::Code::OversizedPayload,
          "BiomePalette", 0 });
    }

    palette.resize(static_cast<std::size_t>(max_index + 1));
    for (const auto& entry : entries) {
      palette[static_cast<std::size_t>(entry.index)] = entry.key;
    }

    return {};
  }

}  // namespace fschema::parser::schem::detail