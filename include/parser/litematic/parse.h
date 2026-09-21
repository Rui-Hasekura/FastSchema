#ifndef FSCHEMA_PARSER_LITEMATIC_PARSE_H_
#define FSCHEMA_PARSER_LITEMATIC_PARSE_H_

#include <cstddef>
#include <expected>
#include <memory>
#include <span>
#include <utility>
#include <vector>

#include "parser/error.h"
#include "parser/limits.h"
#include "parser/litematic/detail/root.h"
#include "parser/litematic/types.h"
#include "parser/nbt/reader.h"

namespace fschema::parser::litematic {

  // Entry: ParseLitematic after DecompressGzipFile
  //
  // bytes' ownership is transferred to the returned Litematic::owner.
  // All internal spans (properties / raw_nbt / preview_data) point to it.
  // This span is valid until the Litematic object is destructed.
  //
  // Precondition: bytes must be a valid gzip decompressed result
  // (guaranteed by the upper layer DecompressGzipFile).
  [[nodiscard]] ParseResult<Litematic> ParseLitematic(
    std::unique_ptr<std::vector<std::byte>> decompressed,
    const DecodeLimits& limits = {});

} // namespace fschema::parser::litematic

#endif // FSCHEMA_PARSER_LITEMATIC_PARSE_H_