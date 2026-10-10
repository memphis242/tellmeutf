// Helpers shared between the translation units of the lookup library. Not part
// of the public contract in ucd.hpp.
#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tellmeutf::ucd::internal {

// ASCII uppercase copy; other bytes are kept as they are.
[[nodiscard]] std::string uppercase(std::string_view text);

// The one word-splitting rule for queries and names (docs/design.md, "Name
// matching"). `text` must already be uppercase. The views in `words` point
// into `text`; `words` is cleared first.
void split_words(std::string_view text, std::vector<std::string_view> &words);

// Calls visit(cp, label) for each table name and each alias, and, when
// `include_hangul` is set, each Hangul syllable name. The label view is valid
// only during the call.
void visit_labels(bool include_hangul, const std::function<void(char32_t, std::string_view)> &visit);

}  // namespace tellmeutf::ucd::internal
