// Helpers shared between the translation units of the lookup library. Not part
// of the public contract in ucd.hpp.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tellmeutf::ucd::internal {

// ASCII uppercase copy of a query without apostrophes (' and U+2019); other
// bytes are kept as they are.
[[nodiscard]] std::string normalize_query(std::string_view text);

// The one word-splitting rule for queries and names (docs/design.md, "Name
// matching"). `text` must already be uppercase. The views in `words` point
// into `text`; `words` is cleared first.
void split_words(std::string_view text, std::vector<std::string_view> &words);

// Where a label comes from.
enum class LabelOrigin : std::uint8_t {
    Official,    // a Unicode name, an alias, or a Hangul syllable name
    Annotation,  // a CLDR short name or keyword
};

// Calls visit(cp, label, origin) for each table name, each alias, each CLDR
// short name and keyword, and, when `include_hangul` is set, each Hangul
// syllable name. The label view is valid only during the call.
void visit_labels(bool include_hangul,
                  const std::function<void(char32_t, std::string_view, LabelOrigin)> &visit);

}  // namespace tellmeutf::ucd::internal
