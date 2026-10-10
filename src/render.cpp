#include "render.hpp"

#include "utf8.hpp"

#include <algorithm>
#include <array>
#include <cassert>

namespace tellmeutf::render {

namespace {

constexpr char32_t kDottedCircle = 0x25CC;
constexpr int kGlyphCells = 2;
constexpr std::size_t kMinCodePointDigits = 4;
constexpr std::string_view kBoldOn = "\x1b[1m";
constexpr std::string_view kBoldOff = "\x1b[0m";

enum class GlyphKind { Character, Mark, Space };

GlyphKind glyph_kind(const ucd::Category category) {
    switch (category) {
    case ucd::Category::Mn:
    case ucd::Category::Mc:
    case ucd::Category::Me:
        return GlyphKind::Mark;
    case ucd::Category::Zs:
    case ucd::Category::Zl:
    case ucd::Category::Zp:
    case ucd::Category::Cc:
    case ucd::Category::Cf:
    case ucd::Category::Cs:
    case ucd::Category::Cn:
        return GlyphKind::Space;
    case ucd::Category::Lu: case ucd::Category::Ll: case ucd::Category::Lt:
    case ucd::Category::Lm: case ucd::Category::Lo:
    case ucd::Category::Nd: case ucd::Category::Nl: case ucd::Category::No:
    case ucd::Category::Pc: case ucd::Category::Pd: case ucd::Category::Ps:
    case ucd::Category::Pe: case ucd::Category::Pi: case ucd::Category::Pf:
    case ucd::Category::Po:
    case ucd::Category::Sm: case ucd::Category::Sc: case ucd::Category::Sk:
    case ucd::Category::So:
    case ucd::Category::Co:
        return GlyphKind::Character;
    }
    assert(false && "unhandled category");
    return GlyphKind::Space;
}

std::string hex_upper(const char32_t value, const std::size_t min_digits) {
    constexpr std::string_view kDigits = "0123456789ABCDEF";
    std::string digits;
    char32_t rest = value;
    do {
        digits.insert(digits.begin(), kDigits[rest & 0xF]);
        rest >>= 4;
    } while (rest != 0);
    if (digits.size() < min_digits) {
        digits.insert(0, min_digits - digits.size(), '0');
    }
    return digits;
}

std::string code_point_cell(const char32_t cp, const std::size_t width) {
    std::string cell = "U+" + hex_upper(cp, kMinCodePointDigits);
    if (cell.size() < width) {
        cell.append(width - cell.size(), ' ');
    }
    return cell;
}

std::string render_row(const struct ucd::CharInfo &info, const bool marker_column, const bool is_target,
                       const std::size_t code_point_width) {
    std::string row;
    if (marker_column) {
        row += is_target ? "> " : "  ";
    }
    row += "0x" + hex_upper(info.cp, 8) + "  ";
    row += code_point_cell(info.cp, code_point_width) + "  ";
    row += glyph_cell(info) + "  ";
    row += info.name;
    return row;
}

}  // namespace

std::string glyph_cell(const struct ucd::CharInfo &info) {
    std::string cell;
    int cells = 1;
    switch (glyph_kind(info.category)) {
    case GlyphKind::Character:
        utf8::append(cell, info.cp);
        cells = info.width;
        break;
    case GlyphKind::Mark:
        utf8::append(cell, kDottedCircle);
        utf8::append(cell, info.cp);
        break;
    case GlyphKind::Space:
        cell = " ";
        break;
    }
    assert(cells >= 1 && cells <= kGlyphCells);
    cell.append(static_cast<std::size_t>(std::max(0, kGlyphCells - cells)), ' ');
    return cell;
}

std::string render_block(const std::vector<ucd::CharInfo> &rows, const std::optional<std::size_t> target,
                         const struct BlockStyle &style) {
    assert(!target || *target < rows.size());
    std::size_t code_point_width = 0;
    for (const struct ucd::CharInfo &info : rows) {
        code_point_width = std::max(code_point_width, code_point_cell(info.cp, 0).size());
    }

    std::string out;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const bool is_target = target && *target == i;
        const bool bold = is_target && style.bold_target;
        if (bold) {
            out += kBoldOn;
        }
        out += render_row(rows[i], style.marker_column, is_target, code_point_width);
        if (bold) {
            out += kBoldOff;
        }
        out.push_back('\n');
    }
    return out;
}

}  // namespace tellmeutf::render
