#include "check.hpp"
#include "render.hpp"
#include "utf8.hpp"

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace render = tellmeutf::render;
namespace ucd = tellmeutf::ucd;
namespace utf8 = tellmeutf::utf8;

namespace {

ucd::CharInfo info(const char32_t cp, const ucd::Category category, std::string name, const int width = 1) {
    return ucd::CharInfo{.cp = cp, .category = category, .name = std::move(name), .width = width};
}

std::string trim_right(std::string text) {
    while (!text.empty() && text.back() == ' ') {
        text.pop_back();
    }
    return text;
}

std::size_t line_count(const std::string &text) {
    return static_cast<std::size_t>(std::count(text.begin(), text.end(), '\n'));
}

bool contains(const std::string &text, const std::string_view needle) {
    return text.find(needle) != std::string::npos;
}

bool has_escape(const std::string &text) { return text.find('\x1b') != std::string::npos; }

std::vector<ucd::CharInfo> neighborhood() {
    return {info(0x276D, ucd::Category::Pe, "FIRST"), info(0x276E, ucd::Category::Ps, "SECOND"),
            info(0x276F, ucd::Category::Pe, "THIRD"), info(0x2770, ucd::Category::Ps, "FOURTH"),
            info(0x2771, ucd::Category::Pe, "FIFTH")};
}

}  // namespace

int main() {
    using ucd::Category;

    // Row prefixes come before every row; without them the output is unchanged.
    {
        const std::vector<ucd::CharInfo> rows = neighborhood();
        const std::string plain = render::render_block(rows, std::nullopt, {});
        CHECK(render::render_block(rows, std::nullopt, {}, {}) == plain);
        const std::vector<std::string> prefixes{"0.91  ", "0.80  ", "0.79  ", "0.60  ", "0.51  "};
        const std::string prefixed = render::render_block(rows, std::nullopt, {}, prefixes);
        CHECK(line_count(prefixed) == rows.size());
        CHECK(prefixed.starts_with("0.91  "));
        std::string expected;
        std::size_t start = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const std::size_t end = plain.find('\n', start);
            expected += prefixes[i] + plain.substr(start, end - start + 1);
            start = end + 1;
        }
        CHECK(prefixed == expected);
    }

    // Glyph cell: the character itself for printable categories.
    for (const Category category :
         {Category::Lu, Category::Ll, Category::Lt, Category::Lm, Category::Lo, Category::Nd, Category::Nl,
          Category::No, Category::Pc, Category::Pd, Category::Ps, Category::Pe, Category::Pi, Category::Pf,
          Category::Po, Category::Sm, Category::Sc, Category::Sk, Category::So, Category::Co}) {
        CHECK(trim_right(render::glyph_cell(info(0x276F, category, "X"))) == utf8::encode(0x276F));
    }

    // Combining marks get a dotted circle in front.
    for (const Category category : {Category::Mn, Category::Mc, Category::Me}) {
        const std::string cell = render::glyph_cell(info(0x301, category, "X"));
        CHECK(trim_right(cell) == utf8::encode(0x25CC) + utf8::encode(0x301));
    }

    // Separators, controls, format characters, surrogates and unassigned code points are a blank.
    for (const Category category : {Category::Zs, Category::Zl, Category::Zp, Category::Cc, Category::Cf,
                                    Category::Cs, Category::Cn}) {
        CHECK(trim_right(render::glyph_cell(info(0x202E, category, "X"))).empty());
    }
    // A wide space is still a one-cell blank.
    CHECK(trim_right(render::glyph_cell(info(0x3000, Category::Zs, "X", 2))).empty());
    CHECK(render::glyph_cell(info(0x3000, Category::Zs, "X", 2)) == render::glyph_cell(info(0x20, Category::Zs, "X")));

    // Padding: a narrow character is padded by one cell, a wide one is not.
    CHECK(render::glyph_cell(info(0x41, Category::Lu, "X", 1)) == "A ");
    CHECK(render::glyph_cell(info(0x4E00, Category::Lo, "X", 2)) == utf8::encode(0x4E00));

    // A block has one row per code point, each ending in a newline.
    const std::vector<ucd::CharInfo> rows = neighborhood();
    for (const bool marker : {false, true}) {
        for (const bool bold : {false, true}) {
            const std::string block = render::render_block(rows, 2, {.marker_column = marker, .bold_target = bold});
            CHECK(line_count(block) == rows.size());
            CHECK(!block.empty() && block.back() == '\n');
            for (const struct ucd::CharInfo &row : rows) {
                CHECK(contains(block, row.name));
            }
            CHECK(contains(block, "0x0000276F"));
            CHECK(contains(block, "U+276F"));
        }
    }

    // The marker appears only with the marker column, and only once.
    {
        const std::string with = render::render_block(rows, 2, {.marker_column = true, .bold_target = false});
        const std::string without = render::render_block(rows, 2, {.marker_column = false, .bold_target = false});
        CHECK(std::count(with.begin(), with.end(), '>') == 1);
        CHECK(!contains(without, ">"));
        const std::size_t marker = with.find('>');
        const std::size_t third = with.find("THIRD");
        const std::size_t second = with.find("SECOND");
        CHECK(marker != std::string::npos && marker > second && marker < third);
    }

    // No target: no marker and no bold, whatever the style asks for.
    {
        const std::string block =
            render::render_block(rows, std::nullopt, {.marker_column = true, .bold_target = true});
        CHECK(!contains(block, ">"));
        CHECK(!has_escape(block));
    }

    // Bold only on request, and only around the target row.
    {
        const std::string plain = render::render_block(rows, 2, {.marker_column = true, .bold_target = false});
        CHECK(!has_escape(plain));
        const std::string bold = render::render_block(rows, 2, {.marker_column = true, .bold_target = true});
        CHECK(has_escape(bold));
        const std::size_t on = bold.find('\x1b');
        CHECK(on > bold.find("SECOND") && on < bold.find("THIRD"));
        CHECK(std::count(bold.begin(), bold.end(), '\x1b') == 2);
        CHECK(bold.find("THIRD") < bold.rfind('\x1b'));
        CHECK(bold.rfind('\x1b') < bold.find("FOURTH"));
    }

    // A target prefix and marker are part of the bold row, while neighboring rows stay plain.
    {
        const std::vector<std::string> prefixes{"0.91  ", "0.80  ", "0.79  ", "0.60  ", "0.51  "};
        const std::string block =
            render::render_block(rows, 2, {.marker_column = true, .bold_target = true}, prefixes);
        const std::size_t bold_on = block.find("\x1b[1m");
        const std::size_t target_prefix = block.find("0.79  > 0x0000276F");
        const std::size_t target_name = block.find("THIRD");
        const std::size_t bold_off = block.find("\x1b[0m");
        CHECK(bold_on != std::string::npos && target_prefix != std::string::npos && bold_on < target_prefix);
        CHECK(target_name != std::string::npos && bold_off != std::string::npos);
        CHECK(target_prefix < target_name && target_name < bold_off);
        CHECK(std::count(block.begin(), block.end(), '\x1b') == 2);
        CHECK(block.find("0.91  ") < bold_on);
        const std::size_t following_name = block.find("FOURTH");
        CHECK(following_name != std::string::npos && following_name > bold_off);
    }

    // The U+ cell is padded to the widest in the block, so the glyph column lines up.
    {
        const std::vector<ucd::CharInfo> mixed = {info(0xFFFF, ucd::Category::Cn, "LOW"),
                                                  info(0x10000, ucd::Category::Lo, "HIGH")};
        const std::string block = render::render_block(mixed, std::nullopt, {});
        CHECK(contains(block, "U+FFFF"));
        CHECK(contains(block, "U+10000"));
        // Names start at the same offset within their lines when the glyph cells have equal byte width.
        const std::vector<ucd::CharInfo> same = {info(0xFFFF, ucd::Category::Cn, "LOW"),
                                                 info(0x10000, ucd::Category::Cn, "HIGH")};
        const std::string aligned = render::render_block(same, std::nullopt, {});
        CHECK(aligned.find("LOW") == aligned.find("HIGH") - aligned.find('\n') - 1);
    }

    // Digits: at least four, uppercase, eight for the UTF-32 value.
    {
        const std::string block = render::render_block({info(0xA, ucd::Category::Cc, "LINE FEED")}, std::nullopt, {});
        CHECK(contains(block, "0x0000000A"));
        CHECK(contains(block, "U+000A"));
        const std::string top = render::render_block({info(0x10FFFF, ucd::Category::Cn, "X")}, std::nullopt, {});
        CHECK(contains(top, "0x0010FFFF"));
        CHECK(contains(top, "U+10FFFF"));
    }

    // An empty block renders to nothing.
    CHECK(render::render_block({}, std::nullopt, {}).empty());

    return tellmeutf::test::finish();
}
