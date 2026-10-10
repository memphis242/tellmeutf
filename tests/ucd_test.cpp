#include "check.hpp"
#include "ucd.hpp"

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace ucd = tellmeutf::ucd;

namespace {

bool contains(const std::vector<char32_t> &list, const char32_t cp) {
    return std::ranges::find(list, cp) != list.end();
}

bool is_ideograph_range(const char32_t cp) {
    const std::string name = ucd::lookup(cp).name;
    return name.starts_with("CJK UNIFIED IDEOGRAPH-") || name.starts_with("TANGUT IDEOGRAPH-") ||
           name.starts_with("JURCHEN CHARACTER-") || name.starts_with("SEAL CHARACTER-");
}

void test_lookup_listed() {
    const struct ucd::CharInfo a = ucd::lookup(0x41);
    CHECK(a.name == "LATIN CAPITAL LETTER A");
    CHECK(a.category == ucd::Category::Lu);
    CHECK(a.width == 1);

    const struct ucd::CharInfo ornament = ucd::lookup(0x276F);
    CHECK(ornament.name == "HEAVY RIGHT-POINTING ANGLE QUOTATION MARK ORNAMENT");
    CHECK(ornament.category == ucd::Category::Pe);

    CHECK(ucd::lookup(0x20).category == ucd::Category::Zs);
    CHECK(ucd::lookup(0x300).category == ucd::Category::Mn);
    CHECK(ucd::lookup(0x3042).width == 2);
    CHECK(ucd::lookup(0xFF21).width == 2);  // fullwidth letter
    CHECK(ucd::lookup(0x41).width == 1);
}

void test_lookup_algorithmic() {
    CHECK(ucd::lookup(0xAC00).name == "HANGUL SYLLABLE GA");
    CHECK(ucd::lookup(0xD7A3).name == "HANGUL SYLLABLE HIH");
    CHECK(ucd::lookup(0xAC00).category == ucd::Category::Lo);

    const struct ucd::CharInfo zhong = ucd::lookup(0x4E2D);
    CHECK(zhong.name == "CJK UNIFIED IDEOGRAPH-4E2D");
    CHECK(zhong.width == 2);
    CHECK(ucd::lookup(0x20000).name == "CJK UNIFIED IDEOGRAPH-20000");
    CHECK(ucd::lookup(0x3D000).name == "SEAL CHARACTER-3D000");
    CHECK(ucd::lookup(0x17000).name == "TANGUT IDEOGRAPH-17000");
}

void test_lookup_labels() {
    CHECK(ucd::lookup(0x0A).name == "LINE FEED");
    CHECK(ucd::lookup(0x0A).category == ucd::Category::Cc);
    CHECK(ucd::lookup(0x80).name == "PADDING CHARACTER");
    CHECK(ucd::lookup(0x378).name == "<unassigned>");
    CHECK(ucd::lookup(0x378).category == ucd::Category::Cn);
    CHECK(ucd::lookup(0xFFFF).name == "<noncharacter>");
    CHECK(ucd::lookup(0xFDD0).name == "<noncharacter>");
    CHECK(ucd::lookup(0x10FFFF).name == "<noncharacter>");
    CHECK(ucd::lookup(0xD800).name == "<surrogate>");
    CHECK(ucd::lookup(0xD800).category == ucd::Category::Cs);
    CHECK(ucd::lookup(0xE000).name == "<private use>");
    CHECK(ucd::lookup(0xE000).category == ucd::Category::Co);
    CHECK(ucd::lookup(0x10FFFD).category == ucd::Category::Co);
}

void test_every_code_point_has_a_name() {
    for (char32_t cp = 0; cp <= ucd::kLastCodePoint; ++cp) {
        const struct ucd::CharInfo info = ucd::lookup(cp);
        CHECK(info.cp == cp);
        CHECK(!info.name.empty());
        CHECK(info.width == 1 || info.width == 2);
        if (info.name.empty()) {
            return;
        }
    }
}

void test_exact() {
    CHECK(ucd::find_exact("heavy right-pointing angle quotation mark ornament") ==
          std::vector<char32_t>{0x276F});
    CHECK(ucd::find_exact("Heavy_Right Pointing  angle-quotation mark_ornament") == std::vector<char32_t>{0x276F});
    CHECK(ucd::find_exact("nbsp") == std::vector<char32_t>{0xA0});
    CHECK(ucd::find_exact("zwj") == std::vector<char32_t>{0x200D});
    CHECK(ucd::find_exact("lf") == std::vector<char32_t>{0x0A});
    CHECK(ucd::find_exact("new line") == std::vector<char32_t>{0x0A});
    CHECK(ucd::find_exact("eol") == std::vector<char32_t>{0x0A});
    CHECK(ucd::find_exact("tibetan letter a") == std::vector<char32_t>{0x0F68});
    CHECK(ucd::find_exact("tibetan letter -a") == std::vector<char32_t>{0x0F60});
    CHECK(ucd::find_exact("hangul jungseong o-e") == std::vector<char32_t>{0x1180});
    CHECK(ucd::find_exact("hangul jungseong oe") == std::vector<char32_t>{0x116C});
    CHECK(ucd::find_exact("hangul syllable ga") == std::vector<char32_t>{0xAC00});
    CHECK(ucd::find_exact("cjk unified ideograph-4e2d") == std::vector<char32_t>{0x4E2D});
    CHECK(ucd::find_exact("CJK UNIFIED IDEOGRAPH-20000") == std::vector<char32_t>{0x20000});
    CHECK(ucd::find_exact("seal character-3d000") == std::vector<char32_t>{0x3D000});
    CHECK(ucd::find_exact("cjk unified ideograph-1f600").empty());  // outside every ideograph range
    CHECK(ucd::find_exact("tangut ideograph-4e2d").empty());        // wrong range for the prefix
    CHECK(ucd::find_exact("cjk unified ideograph-4e").empty());     // too few digits
    CHECK(ucd::find_exact("qwertyuiop").empty());
    CHECK(ucd::find_exact("").empty());
    CHECK(ucd::find_exact(" _ \t ").empty());
    CHECK(ucd::find_exact("<control>").empty());
}

void test_search() {
    const std::vector<char32_t> angle = ucd::search("right angle quotation");
    CHECK(contains(angle, 0x276F));

    const std::vector<char32_t> check = ucd::search("heavy check mark");
    CHECK(!check.empty());
    CHECK(!check.empty() && check.front() == 0x2714);

    // A whole-word match ranks before a prefix-only match, whatever the length.
    const std::vector<char32_t> snow = ucd::search("snowman");
    CHECK(!snow.empty() && snow.front() == 0x2603);
    const std::vector<char32_t> prefix = ucd::search("snowm");
    CHECK(contains(prefix, 0x2603));

    // Word order does not matter; a code point appears once.
    const std::vector<char32_t> shuffled = ucd::search("ornament quot heavy right");
    CHECK(std::ranges::count(shuffled, char32_t{0x276F}) == 1);

    // A whole-word match ranks before a prefix-only match even when its label
    // is longer and its code point higher: BOX DRAWINGS LIGHT DOUBLE DASH
    // HORIZONTAL (U+254C) before LEFTWARDS DASHED ARROW (U+21E0), and the
    // two-word EN DASH (U+2013) before both.
    const std::vector<char32_t> dash = ucd::search("dash");
    const auto en_dash = std::ranges::find(dash, char32_t{0x2013});
    const auto box_dash = std::ranges::find(dash, char32_t{0x254C});
    const auto dashed_arrow = std::ranges::find(dash, char32_t{0x21E0});
    CHECK(en_dash != dash.end() && box_dash != dash.end() && dashed_arrow != dash.end());
    CHECK(en_dash < box_dash);
    CHECK(box_dash < dashed_arrow);

    // Among whole-word matches, fewer label words come first, then lower code
    // point: the four basic arrows lead the results for "arrow".
    const std::vector<char32_t> arrow = ucd::search("arrow");
    CHECK(arrow.size() > 4);
    CHECK(arrow.size() > 4 && arrow[0] == 0x2190 && arrow[1] == 0x2191 && arrow[2] == 0x2192 && arrow[3] == 0x2193);

    CHECK(contains(ucd::search("nbsp"), 0xA0));
    CHECK(contains(ucd::search("hangul syllable gag"), 0xAC01));
    CHECK(ucd::search("").empty());
    CHECK(ucd::search(" _ \t ").empty());
    CHECK(ucd::search("qwertyuiop").empty());
}

// Every named code point must be found again by its own name. find_exact scans
// the tables, so the sweep takes every 37th code point plus all Hangul
// syllables' neighbors and the edges of the listed data.
void test_name_round_trip() {
    std::size_t checked = 0;
    for (char32_t cp = 0; cp <= ucd::kLastCodePoint; ++cp) {
        const struct ucd::CharInfo info = ucd::lookup(cp);
        if (info.name.starts_with('<') || is_ideograph_range(cp)) {
            continue;
        }
        if (cp % 37 != 0 && cp != 0xAC00 && cp != 0xD7A3 && cp != 0x276F) {
            continue;
        }
        ++checked;
        CHECK(contains(ucd::find_exact(info.name), cp));
    }
    CHECK(checked > 500);

    for (const char32_t cp : {char32_t{0x4E00}, char32_t{0x9FFF}, char32_t{0x33479}, char32_t{0x3FC3F}}) {
        const struct ucd::CharInfo info = ucd::lookup(cp);
        CHECK(ucd::find_exact(info.name) == std::vector<char32_t>{cp});
    }
}

void test_metadata() {
    CHECK(ucd::unicode_version() == "18.0.0");
    CHECK(ucd::category_code(ucd::Category::Lu) == "Lu");
    CHECK(ucd::category_code(ucd::Category::Cn) == "Cn");
    // Every category's code is two letters and distinct from the others.
    std::vector<std::string_view> codes;
    for (int c = 0; c <= static_cast<int>(ucd::Category::Cn); ++c) {
        const std::string_view code = ucd::category_code(static_cast<ucd::Category>(c));
        CHECK(code.size() == 2);
        CHECK(std::ranges::find(codes, code) == codes.end());
        codes.push_back(code);
    }
    CHECK(codes.size() == 30);
}

}  // namespace

int main() {
    test_lookup_listed();
    test_lookup_algorithmic();
    test_lookup_labels();
    test_every_code_point_has_a_name();
    test_exact();
    test_search();
    test_name_round_trip();
    test_metadata();
    return tellmeutf::test::finish();
}
