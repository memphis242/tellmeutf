// Unicode Character Database queries: names, general categories, display widths.
//
// All data is compiled in (generated from the vendored UCD files by
// tools/gen_ucd.cpp); no function here touches the file system.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace tellmeutf::ucd {

inline constexpr char32_t kLastCodePoint = 0x10FFFF;

// Unicode General_Category values.
enum class Category : std::uint8_t {
    Lu, Ll, Lt, Lm, Lo,
    Mn, Mc, Me,
    Nd, Nl, No,
    Pc, Pd, Ps, Pe, Pi, Pf, Po,
    Sm, Sc, Sk, So,
    Zs, Zl, Zp,
    Cc, Cf, Cs, Co, Cn,
};

// Two-letter code of a category, for example "Lu".
[[nodiscard]] std::string_view category_code(Category category);

struct CharInfo {
    char32_t cp = 0;
    Category category = Category::Cn;
    // Official name, or the alias name for a control character, or a label in
    // angle brackets for a code point without a name: "<unassigned>",
    // "<noncharacter>", "<private use>", "<surrogate>", "<control>".
    // Never empty.
    std::string name;
    // Terminal cells the character occupies: 2 for East Asian Wide and
    // Fullwidth, otherwise 1.
    int width = 1;
};

// Describes any code point. Precondition: cp <= kLastCodePoint.
[[nodiscard]] CharInfo lookup(char32_t cp);

// Code points whose name or alias equals `name` word for word (case, spacing,
// underscores, medial hyphens and apostrophes are not significant; see
// docs/design.md, "Name matching"). When no name or alias does, those whose
// CLDR short name ("backslash" for U+005C) equals it. Ascending code point
// order. Empty when nothing matches or when `name` has no words.
[[nodiscard]] std::vector<char32_t> find_exact(std::string_view name);

// Code points for which every word of `query` is a prefix of some word of one
// of the code point's labels: its names, aliases, CLDR short name and CLDR
// keywords ("tick" for U+2714). Best match first (see docs/design.md, "Name
// matching"). Each code point appears once. Empty when nothing matches or when
// `query` has no words. Does not enumerate the ideograph ranges.
[[nodiscard]] std::vector<char32_t> search(std::string_view query);

// What a label is.
enum class LabelKind : std::uint8_t {
    Name,       // an official Unicode name
    Alias,      // a name alias
    ShortName,  // the CLDR short name
    Keyword,    // a CLDR keyword
};

// A name, alias, CLDR short name or CLDR keyword together with the code point it
// describes.
struct Label {
    char32_t cp = 0;
    std::string text;
    LabelKind kind = LabelKind::Name;

    [[nodiscard]] bool operator==(const Label &) const = default;
};

// Every name, alias, CLDR short name and CLDR keyword that describes a
// character in words: all explicitly listed names and all aliases, excluding
// the algorithmic names (ideograph ranges, Hangul syllables), the listed names
// that only spell their own code point in hexadecimal ("CJK COMPATIBILITY
// IDEOGRAPH-F900"), and the angle-bracket labels; then the CLDR texts that are
// not already a name or alias of the same code point. Text is as stored (upper
// case ASCII). Order is stable: listed names by code point, then aliases, then
// CLDR texts by code point (short name before keywords).
[[nodiscard]] std::vector<Label> descriptive_labels();

struct FuzzyMatch {
    char32_t cp = 0;
    double score = 0.0;  // similarity to the query, in (0, 1]
    // The evidence behind `score`, used by is_confident():
    double word_score = 0.0;  // word-level similarity, in [0, 1]
    double char_score = 0.0;  // similarity of the label and query with spaces removed, in [0, 1]
    double char_coverage = 0.0;  // share of the query's character trigrams found in the label; 0 for short queries
    // True when some query word matched a label word only weakly (for example
    // by two edits on a short word).
    bool weak_word_match = false;
};

// Approximate best-match retrieval over every label (names, aliases, CLDR short
// names and keywords, as search()): tolerates typos, run-together or split
// words, plural and -ing/-ed/-ly variants, extra or missing words and a
// different word order (see docs/design.md, "Approximate fallback"). Best
// first, at most `limit` entries, each code point once. Empty when the query
// has no words, holds a non-ASCII byte, is too long, or nothing is similar
// enough. The first call builds an index; later calls reuse it. Thread-safe.
[[nodiscard]] std::vector<FuzzyMatch> fuzzy_search(std::string_view query, std::size_t limit);

// True when matches.front() stands clearly above the rest and rests on strong
// evidence from one signal (strong word matches, or the query being the label
// with its spaces removed), so a caller may treat it as the answer rather than
// offer a list. False for an empty list.
[[nodiscard]] bool is_confident(const std::vector<FuzzyMatch> &matches);

// Version of the compiled-in data, for example "18.0.0".
[[nodiscard]] std::string_view unicode_version();

}  // namespace tellmeutf::ucd
