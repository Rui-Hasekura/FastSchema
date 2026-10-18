// Copyright (C) 2026 Rui-Hasekura <ruihasekura@gmail.com>
// SPDX-License-Identifier: Apache-2.0
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fschema/remap/internal/text.h"

#include <charconv>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

#include "fschema/remap/internal/context.h"
#include "fschema/remap/internal/item.h"
#include "fschema/remap/internal/nbt_utils.h"
#include "fschema/remap/internal/rule.h"

namespace fschema::remap::internal {

namespace {

using base::NbtCompound;
using base::NbtList;
using base::NbtPayload;
using base::NbtTag;
using base::TagType;

// Appends `s` to `out` as a JSON string, escaping special characters.
void JsonEscapeInto(std::string& out, std::string_view s) {
  out += '"';
  for (char ch : s) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      default:
        if (static_cast<unsigned char>(ch) < 0x20) {
          char buf[8];
          buf[0] = '\\';
          buf[1] = 'u';
          buf[2] = '0';
          buf[3] = '0';
          constexpr char kHex[] = "0123456789abcdef";
          buf[4] = kHex[(static_cast<unsigned char>(ch) >> 4) & 0xF];
          buf[5] = kHex[static_cast<unsigned char>(ch) & 0xF];
          out.append(buf, 6);
        } else {
          out += ch;  // UTF-8 passthrough
        }
    }
  }
  out += '"';
}

// Appends a floating-point number to `out`.
void JsonNumberInto(std::string& out, double v) {
  char buf[64];
  const auto res = std::to_chars(buf, buf + sizeof(buf), v);
  if (res.ec == std::errc{}) {
    out.append(buf, static_cast<std::size_t>(res.ptr - buf));
  } else {
    out += '0';
  }
}

void JsonValueInto(std::string& out,
                   TagType type,
                   const NbtPayload& p,
                   int depth);

void JsonObjectInto(std::string& out, const NbtCompound& c, int depth);

// Converts an NBT text component to JSON. Returns false when `type` is not
// a string / list / compound.
bool TextComponentInto(std::string& out,
                       TagType type,
                       const NbtPayload& p,
                       int depth);

// click_event: source form uses typed field names (command / url / page);
// target JSON uses {"action": ..., "value": ...} uniformly.
void ClickEventInto(std::string& out, const NbtCompound& c) {
  out += '{';
  std::string_view action;
  std::string_view value_field;
  const NbtTag* value_tag = nullptr;
  for (const auto& f : c.children) {
    if (f.name == "action") {
      action = AsString(f);
    } else {
      value_field = f.name;  // command / url / page / value / key
      value_tag = &f;
    }
  }
  out += "\"action\":";
  JsonEscapeInto(out, action);
  out += ",\"value\":";
  if (value_tag != nullptr && value_tag->type == TagType::kString) {
    JsonEscapeInto(out, AsString(*value_tag));
  } else if (value_tag != nullptr) {
    JsonValueInto(out, value_tag->type, value_tag->payload, 2);
  } else {
    out += "\"\"";
  }
  out += '}';
}

// Lightweight JSON string escaper that only handles `"` and `\`.
void AppendJsonStringEscaped(std::string& out, std::string_view s) {
  out += '"';
  for (char ch : s) {
    switch (ch) {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      default:
        out += ch;
        break;
    }
  }
  out += '"';
}

void HoverEventInto(std::string& out, const NbtCompound& c, int depth) {
  std::string_view action;
  for (const auto& f : c.children) {
    if (f.name == "action") {
      action = AsString(f);
      break;
    }
  }

  out += '{';
  out += "\"action\":";
  JsonEscapeInto(out, action);

  const NbtTag* payload_tag = FindChild(c, "value");
  if (payload_tag == nullptr) {
    payload_tag = FindChild(c, "contents");
  }

  if (action == "show_item") {
    if (payload_tag == nullptr) {
      out += ",\"value\":\"{}\"}";
      return;
    }

    if (payload_tag->type == TagType::kString) {
      out += ",\"value\":";
      JsonEscapeInto(out, AsString(*payload_tag));
      out += '}';
      return;
    }

    const NbtCompound* tc = AsCompound(*payload_tag);
    if (tc != nullptr) {
      const NbtTag* id = FindChild(*tc, "id");
      const NbtTag* count = FindChild(*tc, "Count");
      const NbtTag* tag = FindChild(*tc, "tag");

      out += ",\"value\":\"{id:";
      if (id != nullptr) {
        AppendJsonStringEscaped(out, AsString(*id));
      } else {
        out += "\"\"";
      }

      std::int64_t c_val = 1;
      if (count != nullptr) {
        if (const auto v = IntOf(*count)) c_val = *v;
      }
      out += ",Count:";
      out += std::to_string(c_val);
      out += "b";

      if (tag != nullptr) {
        out += ",tag:";
        JsonValueInto(out, tag->type, tag->payload, depth + 1);
      }
      out += "}\"}";
    } else {
      out += ",\"value\":\"{}\"}";
    }
    return;
  }

  if (action == "show_entity") {
    if (payload_tag == nullptr) {
      out += ",\"value\":\"{}\"}";
      return;
    }

    if (payload_tag->type == TagType::kString) {
      out += ",\"value\":";
      JsonEscapeInto(out, AsString(*payload_tag));
      out += '}';
      return;
    }

    const NbtCompound* tc = AsCompound(*payload_tag);
    if (tc != nullptr) {
      // Source form: `uuid` + `id`. Fallback for older forms: `id` + `type`.
      const NbtTag* uuid = FindChild(*tc, "uuid");
      const NbtTag* type = FindChild(*tc, "type");
      const NbtTag* name = FindChild(*tc, "name");

      if (uuid == nullptr) uuid = FindChild(*tc, "id");
      if (type == nullptr) type = FindChild(*tc, "id");

      out += ",\"value\":\"{id:";
      if (uuid != nullptr) {
        if (uuid->type == TagType::kString) {
          AppendJsonStringEscaped(out, AsString(*uuid));
        } else if (uuid->type == TagType::kIntArray) {
          if (const auto* raw =
                  std::get_if<std::span<const std::byte>>(&uuid->payload)) {
            out += "[I;";
            const auto ints = ReadIntArray(*raw);
            for (std::size_t i = 0; i < ints.size(); ++i) {
              if (i != 0) out += ',';
              out += std::to_string(ints[i]);
            }
            out += "]";
          } else {
            out += "\"\"";
          }
        } else {
          out += "\"\"";
        }
      } else {
        out += "\"\"";
      }

      out += ",type:";
      if (type != nullptr) {
        AppendJsonStringEscaped(out, AsString(*type));
      } else {
        out += "\"\"";
      }

      if (name != nullptr) {
        out += ",name:";
        if (!TextComponentInto(out, name->type, name->payload, depth + 1)) {
          out += "\"\"";
        }
      }
      out += "}\"}";
    } else {
      out += ",\"value\":\"{}\"}";
    }
    return;
  }

  // show_text or unknown.
  out += ",\"contents\":";
  if (payload_tag != nullptr) {
    if (!TextComponentInto(
            out, payload_tag->type, payload_tag->payload, depth + 1)) {
      JsonValueInto(out, payload_tag->type, payload_tag->payload, depth + 1);
    }
  } else {
    out += "\"\"";
  }
  out += '}';
}

bool TextComponentInto(std::string& out,
                       TagType type,
                       const NbtPayload& p,
                       int depth) {
  // Depth cap for malformed recursive text components.
  if (depth > 32) return false;
  if (type == TagType::kString) {
    if (const auto* sv = std::get_if<std::string_view>(&p)) {
      JsonEscapeInto(out, *sv);
      return true;
    }
    return false;
  }
  if (type == TagType::kList) {
    const auto* lp = std::get_if<std::unique_ptr<NbtList>>(&p);
    if (lp == nullptr || *lp == nullptr) return false;
    out += '[';
    bool first = true;
    for (const auto& e : (*lp)->children) {
      if (!first) out += ',';
      first = false;
      if (!TextComponentInto(out, (*lp)->element_type, e, depth + 1)) {
        JsonValueInto(out, (*lp)->element_type, e, depth + 1);
      }
    }
    out += ']';
    return true;
  }
  if (type == TagType::kCompound) {
    const auto* cp = std::get_if<std::unique_ptr<NbtCompound>>(&p);
    if (cp == nullptr || *cp == nullptr) return false;
    JsonObjectInto(out, **cp, depth + 1);
    return true;
  }
  return false;
}

void JsonObjectInto(std::string& out, const NbtCompound& c, int depth) {
  if (depth > 32) {
    out += "{}";
    return;
  }
  out += '{';
  bool first = true;
  for (const auto& f : c.children) {
    if (!first) out += ',';
    first = false;
    // click_event / hover_event are rewritten to their JSON shape.
    std::string_view key = f.name;
    if (key == "click_event") {
      out += "\"clickEvent\":";
      if (AsCompound(f) != nullptr) {
        ClickEventInto(out, *AsCompound(f));
      } else {
        JsonValueInto(out, f.type, f.payload, depth + 1);
      }
      continue;
    }
    if (key == "hover_event") {
      out += "\"hoverEvent\":";
      if (AsCompound(f) != nullptr) {
        HoverEventInto(out, *AsCompound(f), depth);
      } else {
        JsonValueInto(out, f.type, f.payload, depth + 1);
      }
      continue;
    }
    out += '"';
    out += key;
    out += "\":";
    // Boolean style keys: TAG_Byte 0/1 -> true/false.
    if (f.type == TagType::kByte &&
        (key == "bold" || key == "italic" || key == "underlined" ||
         key == "strikethrough" || key == "obfuscated")) {
      const auto* v = std::get_if<std::int8_t>(&f.payload);
      out += (v != nullptr && *v != 0) ? "true" : "false";
      continue;
    }
    if (!TextComponentInto(out, f.type, f.payload, depth + 1)) {
      JsonValueInto(out, f.type, f.payload, depth + 1);
    }
  }
  out += '}';
}

void JsonValueInto(std::string& out,
                   TagType type,
                   const NbtPayload& p,
                   int depth) {
  if (depth > 32) {
    out += "null";
    return;
  }
  switch (type) {
    case TagType::kByte:
      out += std::to_string(static_cast<int>(
          std::get_if<std::int8_t>(&p) ? *std::get_if<std::int8_t>(&p) : 0));
      break;
    case TagType::kShort:
      out += std::to_string(static_cast<int>(
          std::get_if<std::int16_t>(&p) ? *std::get_if<std::int16_t>(&p) : 0));
      break;
    case TagType::kInt:
      out += std::to_string(
          std::get_if<std::int32_t>(&p) ? *std::get_if<std::int32_t>(&p) : 0);
      break;
    case TagType::kLong:
      out += std::to_string(
          std::get_if<std::int64_t>(&p) ? *std::get_if<std::int64_t>(&p) : 0);
      break;
    case TagType::kFloat: {
      const auto* v = std::get_if<float>(&p);
      JsonNumberInto(out, v ? static_cast<double>(*v) : 0.0);
      break;
    }
    case TagType::kDouble: {
      const auto* v = std::get_if<double>(&p);
      JsonNumberInto(out, v ? *v : 0.0);
      break;
    }
    case TagType::kString:
      JsonEscapeInto(out,
                     std::get_if<std::string_view>(&p)
                         ? *std::get_if<std::string_view>(&p)
                         : std::string_view{});
      break;
    case TagType::kCompound: {
      const auto* cp = std::get_if<std::unique_ptr<NbtCompound>>(&p);
      if (cp != nullptr && *cp != nullptr) {
        JsonObjectInto(out, **cp, depth + 1);
      } else {
        out += "{}";
      }
      break;
    }
    case TagType::kList: {
      const auto* lp = std::get_if<std::unique_ptr<NbtList>>(&p);
      if (lp != nullptr && *lp != nullptr) {
        out += '[';
        bool first = true;
        for (const auto& e : (*lp)->children) {
          if (!first) out += ',';
          first = false;
          JsonValueInto(out, (*lp)->element_type, e, depth + 1);
        }
        out += ']';
      } else {
        out += "[]";
      }
      break;
    }
    case TagType::kByteArray: {
      const auto* v = std::get_if<std::span<const std::int8_t>>(&p);
      out += '[';
      if (v != nullptr) {
        for (std::size_t i = 0; i < v->size(); ++i) {
          if (i != 0) out += ',';
          out += std::to_string(static_cast<int>((*v)[i]));
        }
      }
      out += ']';
      break;
    }
    case TagType::kIntArray: {
      const auto* v = std::get_if<std::span<const std::byte>>(&p);
      out += '[';
      if (v != nullptr) {
        const std::size_t n = v->size() / sizeof(std::int32_t);
        for (std::size_t i = 0; i < n; ++i) {
          if (i != 0) out += ',';
          std::uint32_t be = 0;
          for (int b = 0; b < 4; ++b) {
            be = (be << 8) | static_cast<std::uint8_t>((*v)[i * 4 + b]);
          }
          out += std::to_string(static_cast<std::int32_t>(be));
        }
      }
      out += ']';
      break;
    }
    case TagType::kLongArray: {
      const auto* v = std::get_if<std::span<const std::byte>>(&p);
      out += '[';
      if (v != nullptr) {
        const std::size_t n = v->size() / sizeof(std::int64_t);
        for (std::size_t i = 0; i < n; ++i) {
          if (i != 0) out += ',';
          std::uint64_t be = 0;
          for (int b = 0; b < 8; ++b) {
            be = (be << 8) | static_cast<std::uint8_t>((*v)[i * 8 + b]);
          }
          out += std::to_string(static_cast<std::int64_t>(be));
        }
      }
      out += ']';
      break;
    }
    case TagType::kEnd:
      out += "null";
      break;
  }
}

}  // namespace

// Wraps a plain-text string into the target-era JSON text-component form
// (`{"text": "<escaped>"}`).
std::string WrapPlainTextAsJson(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 16);
  out += "{\"text\":";
  JsonEscapeInto(out, text);
  out += "}";
  return out;
}

// Converts a structured (NBT) text component into its JSON string form.
// Returns nullopt when the input is not a compound or list.
std::optional<std::string> StructuredTextToJson(base::TagType type,
                                                const base::NbtPayload& p) {
  if (type == TagType::kString) return std::nullopt;  // already target form
  if (type != TagType::kCompound && type != TagType::kList) {
    return std::nullopt;
  }
  std::string out;
  out.reserve(64);
  if (!TextComponentInto(out, type, p, 0)) return std::nullopt;
  return out;
}

// Walks a text component tree and pre-converts any `show_item` payloads
// inside `hover_event` to the target-era item tag form.
void PreprocessTextHoverItems(RemapContext& ctx,
                              base::NbtCompound& c,
                              std::deque<std::string>& scratch,
                              std::string_view path) {
  for (auto& child : c.children) {
    if (child.name == "hoverEvent" || child.name == "hover_event") {
      if (child.type == TagType::kCompound) {
        auto* hc = std::get_if<std::unique_ptr<NbtCompound>>(&child.payload);
        if (hc && *hc) {
          NbtTag* action_tag = FindChild(**hc, "action");
          if (action_tag && AsString(*action_tag) == "show_item") {
            NbtTag* payload_tag = FindChild(**hc, "value");
            if (!payload_tag) {
              payload_tag = FindChild(**hc, "contents");
            }
            if (payload_tag && payload_tag->type == TagType::kCompound) {
              auto* ic = std::get_if<std::unique_ptr<NbtCompound>>(
                  &payload_tag->payload);
              if (ic && *ic) {
                if (FindChild(**ic, "components") != nullptr ||
                    FindChild(**ic, "count") != nullptr) {
                  DowngradeItemCompound(ctx, **ic, scratch, path);
                }
              }
            }
          }
        }
      }
    } else if (child.type == TagType::kCompound) {
      if (auto* cp =
              std::get_if<std::unique_ptr<NbtCompound>>(&child.payload)) {
        if (*cp) {
          PreprocessTextHoverItems(ctx, **cp, scratch, path);
        }
      }
    } else if (child.type == TagType::kList) {
      if (auto* lp = std::get_if<std::unique_ptr<NbtList>>(&child.payload)) {
        if (*lp) {
          for (auto& e : (*lp)->children) {
            if (auto* ec = std::get_if<std::unique_ptr<NbtCompound>>(&e)) {
              if (*ec) {
                PreprocessTextHoverItems(ctx, **ec, scratch, path);
              }
            }
          }
        }
      }
    }
  }
}

// Downgrades the text component stored under `field` in `parent` to JSON.
// No-op when the field is absent.
void DowngradeTextFieldInPlace(RemapContext& ctx,
                               base::NbtCompound& parent,
                               std::string_view field,
                               std::uint16_t rule_id,
                               std::string_view path,
                               std::deque<std::string>& scratch) {
  NbtTag* child = FindChild(parent, field);
  if (child == nullptr) return;

  if (child->type == TagType::kString) {
    if (ctx.src_dv() < kDvComponents) return;
    const std::string_view raw = AsString(*child);
    // A String value that already begins with `{`, `[`, or a matched pair
    // of quotes is a JSON text component and must be left as-is.
    const bool looks_like_json =
        !raw.empty() &&
        (raw.front() == '{' || raw.front() == '[' ||
         (raw.front() == '"' && raw.size() > 1 && raw.back() == '"'));
    if (looks_like_json) return;
    scratch.push_back(WrapPlainTextAsJson(raw));
    child->payload = std::string_view(scratch.back());
    ++ctx.stats().counters.texts_downgraded;
    ctx.Report(Diag::kTextDowngraded, rule_id, path);
    return;
  }

  if (child->type != TagType::kCompound && child->type != TagType::kList) {
    return;
  }

  if (child->type == TagType::kCompound) {
    if (auto* cp = std::get_if<std::unique_ptr<NbtCompound>>(&child->payload)) {
      if (*cp) {
        PreprocessTextHoverItems(ctx, **cp, scratch, path);
      }
    }
  } else if (child->type == TagType::kList) {
    if (auto* lp = std::get_if<std::unique_ptr<NbtList>>(&child->payload)) {
      if (*lp) {
        for (auto& e : (*lp)->children) {
          if (auto* cp = std::get_if<std::unique_ptr<NbtCompound>>(&e)) {
            if (*cp) {
              PreprocessTextHoverItems(ctx, **cp, scratch, path);
            }
          }
        }
      }
    }
  }

  auto json = StructuredTextToJson(child->type, child->payload);
  if (!json.has_value()) {
    ++ctx.stats().counters.text_unsupported_shapes;
    ctx.Report(Diag::kTextUnsupportedShape, rule_id, path);
    return;
  }
  scratch.push_back(std::move(*json));
  child->type = TagType::kString;
  child->payload = std::string_view(scratch.back());
  ++ctx.stats().counters.texts_downgraded;
  ctx.Report(Diag::kTextDowngraded, rule_id, path);
}

}  // namespace fschema::remap::internal