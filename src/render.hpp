// Rendering of character rows and blocks as UTF-8 text. Pure string building.
#pragma once

#include "ucd.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace tellmeutf::render {

struct BlockStyle {
    // Prefix every row with a marker column ('>' on the target row).
    bool marker_column = false;
    // Wrap the target row in bold escape sequences.
    bool bold_target = false;
};

// The glyph cell of `info`, padded on the right with spaces to 2 terminal
// cells: the character itself for most categories, a dotted circle followed by
// the character for combining marks, and a single space for separators,
// controls, format characters, surrogates and unassigned code points.
[[nodiscard]] std::string glyph_cell(const struct ucd::CharInfo &info);

// One line per element of `rows`, each ending in a newline. `target`, when
// present, is the index of the row to mark and (per `style`) emphasize.
[[nodiscard]] std::string render_block(const std::vector<ucd::CharInfo> &rows,
                                       std::optional<std::size_t> target, const struct BlockStyle &style);

}  // namespace tellmeutf::render
