// Lookup and name search over the generated tables (see ucd_tables.hpp).
#include "ucd.hpp"

#include "ucd_internal.hpp"
#include "ucd_tables.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <charconv>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace tellmeutf::ucd {

namespace {

namespace tbl = ::tellmeutf::ucd::tables;

// ---------------------------------------------------------------------------
// Words

[[nodiscard]] constexpr bool is_word_alnum(const char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

[[nodiscard]] constexpr bool is_word_space(const char c) {
    switch (c) {
    case ' ':
    case '\t':
    case '\n':
    case '\r':
    case '\v':
    case '\f':
    case '_':
        return true;
    default:
        return false;
    }
}

}  // namespace

namespace internal {

// ASCII uppercase copy; other bytes are kept as they are.
std::string uppercase(const std::string_view text) {
    std::string result(text);
    for (char &c : result) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return result;
}

// The one word-splitting rule for queries and names (docs/design.md, "Name
// matching"). `text` must already be uppercase. Words are separated by
// whitespace, underscores, and hyphens between two letters or digits (medial
// hyphens); any other hyphen stays inside its word. The views in `words` point
// into `text`; `words` is cleared first.
void split_words(const std::string_view text, std::vector<std::string_view> &words) {
    words.clear();
    std::size_t word_start = std::string_view::npos;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        const bool medial_hyphen =
            c == '-' && i > 0 && i + 1 < text.size() && is_word_alnum(text[i - 1]) && is_word_alnum(text[i + 1]);
        if (is_word_space(c) || medial_hyphen) {
            if (word_start != std::string_view::npos) {
                words.push_back(text.substr(word_start, i - word_start));
                word_start = std::string_view::npos;
            }
        } else if (word_start == std::string_view::npos) {
            word_start = i;
        }
    }
    if (word_start != std::string_view::npos) {
        words.push_back(text.substr(word_start));
    }
}

}  // namespace internal

using internal::split_words;
using internal::uppercase;

namespace {

// ---------------------------------------------------------------------------
// Tables

[[nodiscard]] std::string_view name_text(const struct tbl::NameRef ref) {
    assert(static_cast<std::size_t>(ref.offset) + ref.length <= tbl::kTables.names.size());
    return tbl::kTables.names.substr(ref.offset, ref.length);
}

[[nodiscard]] const struct tbl::Entry *find_entry(const char32_t cp) {
    const std::span<const struct tbl::Entry> entries = tbl::kTables.entries;
    const auto it = std::ranges::lower_bound(entries, cp, {}, &tbl::Entry::cp);
    return it != entries.end() && it->cp == cp ? &*it : nullptr;
}

[[nodiscard]] const struct tbl::Range *find_range(const char32_t cp) {
    for (const struct tbl::Range &range : tbl::kTables.ranges) {
        if (cp >= range.first && cp <= range.last) {
            return &range;
        }
    }
    return nullptr;
}

[[nodiscard]] std::span<const struct tbl::Alias> aliases_of(const char32_t cp) {
    const std::span<const struct tbl::Alias> aliases = tbl::kTables.aliases;
    const auto bounds = std::ranges::equal_range(aliases, cp, {}, &tbl::Alias::cp);
    return std::span<const struct tbl::Alias>(bounds.begin(), bounds.end());
}

[[nodiscard]] bool is_wide(const char32_t cp) {
    const std::span<const struct tbl::WideRange> wide = tbl::kTables.wide;
    // First range whose last code point is >= cp.
    const auto it = std::ranges::lower_bound(wide, cp, {}, &tbl::WideRange::last);
    return it != wide.end() && it->first <= cp;
}

[[nodiscard]] constexpr bool is_noncharacter(const char32_t cp) {
    return (cp >= 0xFDD0 && cp <= 0xFDEF) || (cp & 0xFFFE) == 0xFFFE;
}

// Display name of a control: its first control alias, else its first figment
// alias, else "<control>".
[[nodiscard]] std::string control_name(const char32_t cp) {
    const std::span<const struct tbl::Alias> aliases = aliases_of(cp);
    for (const tbl::AliasType wanted : {tbl::AliasType::Control, tbl::AliasType::Figment}) {
        for (const struct tbl::Alias &alias : aliases) {
            if (alias.type == wanted) {
                return std::string(name_text(alias.name));
            }
        }
    }
    return "<control>";
}

// ---------------------------------------------------------------------------
// Hangul syllables (Unicode core specification 3.12, short names from Jamo.txt,
// which is frozen).

constexpr char32_t kHangulFirst = 0xAC00;
constexpr char32_t kHangulLast = 0xD7A3;
constexpr char32_t kVowelCount = 21;
constexpr char32_t kTrailCount = 28;

constexpr std::array<std::string_view, 19> kLeadNames{"G", "GG", "N", "D", "DD", "R", "M", "B", "BB", "S",
                                                      "SS", "", "J", "JJ", "C", "K", "T", "P", "H"};
constexpr std::array<std::string_view, kVowelCount> kVowelNames{"A",  "AE", "YA", "YAE", "EO", "E", "YEO",
                                                                "YE", "O",  "WA", "WAE", "OE", "YO", "U",
                                                                "WEO", "WE", "WI", "YU", "EU", "YI", "I"};
constexpr std::array<std::string_view, kTrailCount> kTrailNames{
    "", "G", "GG", "GS", "N", "NJ", "NH", "D", "L", "LG", "LM", "LB", "LS", "LT",
    "LP", "LH", "M", "B", "BS", "S", "SS", "NG", "J", "C", "K", "T", "P", "H"};

constexpr std::string_view kHangulPrefix = "HANGUL SYLLABLE ";

[[nodiscard]] std::string hangul_name(const char32_t cp) {
    assert(cp >= kHangulFirst && cp <= kHangulLast);
    const char32_t index = cp - kHangulFirst;
    const char32_t lead = index / (kVowelCount * kTrailCount);
    const char32_t vowel = (index % (kVowelCount * kTrailCount)) / kTrailCount;
    const char32_t trail = index % kTrailCount;
    std::string name(kHangulPrefix);
    name += kLeadNames[lead];
    name += kVowelNames[vowel];
    name += kTrailNames[trail];
    return name;
}

// ---------------------------------------------------------------------------
// Algorithmic ideograph names

// "CJK UNIFIED IDEOGRAPH-" and friends; empty for rules that are not named by
// a prefix and a hexadecimal code point.
[[nodiscard]] constexpr std::string_view ideograph_prefix(const tbl::RangeRule rule) {
    switch (rule) {
    case tbl::RangeRule::CjkIdeograph:
        return "CJK UNIFIED IDEOGRAPH-";
    case tbl::RangeRule::TangutIdeograph:
        return "TANGUT IDEOGRAPH-";
    case tbl::RangeRule::JurchenCharacter:
        return "JURCHEN CHARACTER-";
    case tbl::RangeRule::SealCharacter:
        return "SEAL CHARACTER-";
    case tbl::RangeRule::HangulSyllable:
    case tbl::RangeRule::Surrogate:
    case tbl::RangeRule::PrivateUse:
        return {};
    }
    return {};
}

// Uppercase hexadecimal with at least 4 digits.
[[nodiscard]] std::string canonical_hex(const unsigned value) {
    char digits[16];
    const int length = std::snprintf(digits, sizeof digits, "%04X", value);
    assert(length > 0 && static_cast<std::size_t>(length) < sizeof digits);
    return std::string(digits, static_cast<std::size_t>(length));
}

[[nodiscard]] std::string range_name(const tbl::RangeRule rule, const char32_t cp) {
    switch (rule) {
    case tbl::RangeRule::HangulSyllable:
        return hangul_name(cp);
    case tbl::RangeRule::Surrogate:
        return "<surrogate>";
    case tbl::RangeRule::PrivateUse:
        return "<private use>";
    case tbl::RangeRule::CjkIdeograph:
    case tbl::RangeRule::TangutIdeograph:
    case tbl::RangeRule::JurchenCharacter:
    case tbl::RangeRule::SealCharacter: {
        return std::string(ideograph_prefix(rule)) + canonical_hex(static_cast<unsigned>(cp));
    }
    }
    return {};
}

// Code point named by `words` when they spell an algorithmic ideograph name
// (prefix words plus 4 to 6 hexadecimal digits) inside a range of the matching
// rule; `words` come from split_words, so the prefix "CJK UNIFIED IDEOGRAPH-"
// reads as the words CJK UNIFIED IDEOGRAPH <hex>.
void find_ideograph(const std::vector<std::string_view> &words, std::vector<char32_t> &found) {
    if (words.size() < 2) {
        return;
    }
    const std::string_view hex = words.back();
    if (hex.size() < 4 || hex.size() > 6) {
        return;
    }
    unsigned value = 0;
    const auto [end, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
    if (ec != std::errc{} || end != hex.data() + hex.size()) {
        return;
    }
    // Only the canonical rendering is a name: uppercase digits, at least 4,
    // no extra leading zeros.
    if (hex != canonical_hex(value)) {
        return;
    }
    const struct tbl::Range *const range = find_range(static_cast<char32_t>(value));
    if (range == nullptr) {
        return;
    }
    const std::string_view prefix = ideograph_prefix(range->rule);
    if (prefix.empty()) {
        return;
    }
    // Compare the query's leading words with the prefix words.
    const std::string prefix_text(prefix.substr(0, prefix.size() - 1));  // without the trailing '-'
    std::vector<std::string_view> prefix_words;
    split_words(prefix_text, prefix_words);
    if (std::ranges::equal(prefix_words, std::span(words).first(words.size() - 1))) {
        found.push_back(static_cast<char32_t>(value));
    }
}

// ---------------------------------------------------------------------------
// Labels: every searchable (code point, name) pair

// Calls visit(cp, label) for each table name, each alias, and each Hangul
// syllable name. Labels in angle brackets are not names and are skipped.
template <class Visit>
void for_each_label(Visit &&visit) {
    for (const struct tbl::Entry &entry : tbl::kTables.entries) {
        const std::string_view name = name_text(entry.name);
        if (!name.starts_with('<')) {
            visit(entry.cp, name);
        }
    }
    for (const struct tbl::Alias &alias : tbl::kTables.aliases) {
        visit(alias.cp, name_text(alias.name));
    }
    for (char32_t cp = kHangulFirst; cp <= kHangulLast; ++cp) {
        const std::string name = hangul_name(cp);
        visit(cp, std::string_view(name));
    }
}

}  // namespace

namespace internal {

void visit_labels(const bool include_hangul, const std::function<void(char32_t, std::string_view)> &visit) {
    if (include_hangul) {
        for_each_label(visit);
        return;
    }
    for (const struct tbl::Entry &entry : tbl::kTables.entries) {
        const std::string_view name = name_text(entry.name);
        if (!name.starts_with('<')) {
            visit(entry.cp, name);
        }
    }
    for (const struct tbl::Alias &alias : tbl::kTables.aliases) {
        visit(alias.cp, name_text(alias.name));
    }
}

}  // namespace internal

std::string_view category_code(const Category category) {
    switch (category) {
    case Category::Lu: return "Lu";
    case Category::Ll: return "Ll";
    case Category::Lt: return "Lt";
    case Category::Lm: return "Lm";
    case Category::Lo: return "Lo";
    case Category::Mn: return "Mn";
    case Category::Mc: return "Mc";
    case Category::Me: return "Me";
    case Category::Nd: return "Nd";
    case Category::Nl: return "Nl";
    case Category::No: return "No";
    case Category::Pc: return "Pc";
    case Category::Pd: return "Pd";
    case Category::Ps: return "Ps";
    case Category::Pe: return "Pe";
    case Category::Pi: return "Pi";
    case Category::Pf: return "Pf";
    case Category::Po: return "Po";
    case Category::Sm: return "Sm";
    case Category::Sc: return "Sc";
    case Category::Sk: return "Sk";
    case Category::So: return "So";
    case Category::Zs: return "Zs";
    case Category::Zl: return "Zl";
    case Category::Zp: return "Zp";
    case Category::Cc: return "Cc";
    case Category::Cf: return "Cf";
    case Category::Cs: return "Cs";
    case Category::Co: return "Co";
    case Category::Cn: return "Cn";
    }
    return "Cn";
}

CharInfo lookup(const char32_t cp) {
    assert(cp <= kLastCodePoint);
    struct CharInfo info;
    info.cp = cp;
    info.width = is_wide(cp) ? 2 : 1;

    if (const struct tbl::Entry *const entry = find_entry(cp)) {
        info.category = entry->category;
        const std::string_view name = name_text(entry->name);
        info.name = entry->category == Category::Cc && name == "<control>" ? control_name(cp) : std::string(name);
    } else if (const struct tbl::Range *const range = find_range(cp)) {
        info.category = range->category;
        info.name = range_name(range->rule, cp);
    } else {
        info.category = Category::Cn;
        info.name = is_noncharacter(cp) ? "<noncharacter>" : "<unassigned>";
    }
    assert(!info.name.empty());
    return info;
}

std::vector<char32_t> find_exact(const std::string_view name) {
    const std::string query = uppercase(name);
    std::vector<std::string_view> query_words;
    split_words(query, query_words);
    if (query_words.empty()) {
        return {};
    }

    std::vector<char32_t> found;
    std::vector<std::string_view> label_words;
    for_each_label([&](const char32_t cp, const std::string_view label) {
        split_words(label, label_words);
        if (std::ranges::equal(label_words, query_words)) {
            found.push_back(cp);
        }
    });
    find_ideograph(query_words, found);

    std::ranges::sort(found);
    const auto duplicates = std::ranges::unique(found);
    found.erase(duplicates.begin(), duplicates.end());
    return found;
}

std::vector<char32_t> search(const std::string_view query) {
    const std::string upper = uppercase(query);
    std::vector<std::string_view> query_words;
    split_words(upper, query_words);
    if (query_words.empty()) {
        return {};
    }

    // One candidate per matching label: tier 0 when every query word equals a
    // whole word of the label, tier 1 when some are only prefixes. Within a
    // tier, labels with fewer words are closer to the query.
    struct Candidate {
        int tier = 0;
        std::size_t words = 0;
        char32_t cp = 0;
    };
    std::vector<struct Candidate> candidates;
    std::vector<std::string_view> label_words;
    for_each_label([&](const char32_t cp, const std::string_view label) {
        split_words(label, label_words);
        bool all_whole = true;
        for (const std::string_view query_word : query_words) {
            bool prefix = false;
            bool whole = false;
            for (const std::string_view word : label_words) {
                if (word.starts_with(query_word)) {
                    prefix = true;
                    if (word.size() == query_word.size()) {
                        whole = true;
                        break;
                    }
                }
            }
            if (!prefix) {
                return;
            }
            all_whole = all_whole && whole;
        }
        candidates.push_back(Candidate{.tier = all_whole ? 0 : 1, .words = label_words.size(), .cp = cp});
    });

    // Keep the best label of each code point, then order by rank.
    std::ranges::sort(candidates, [](const struct Candidate &a, const struct Candidate &b) {
        return std::tie(a.cp, a.tier, a.words) < std::tie(b.cp, b.tier, b.words);
    });
    const auto duplicates =
        std::ranges::unique(candidates, [](const struct Candidate &a, const struct Candidate &b) { return a.cp == b.cp; });
    candidates.erase(duplicates.begin(), duplicates.end());
    std::ranges::sort(candidates, [](const struct Candidate &a, const struct Candidate &b) {
        return std::tie(a.tier, a.words, a.cp) < std::tie(b.tier, b.words, b.cp);
    });

    std::vector<char32_t> result;
    result.reserve(candidates.size());
    std::ranges::transform(candidates, std::back_inserter(result), &Candidate::cp);
    return result;
}

std::string_view unicode_version() { return tbl::kTables.version; }

}  // namespace tellmeutf::ucd
