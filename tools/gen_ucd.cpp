// Build-time generator: reads the vendored Unicode Character Database files and
// the vendored CLDR English annotations, and writes one C++ source file holding
// the lookup tables declared in src/ucd_tables.hpp.
//
//   gen_ucd <UnicodeData.txt> <NameAliases.txt> <EastAsianWidth.txt> <annotations/en.xml> -o <output.cpp>
//
// Malformed input is reported as "file:line: message" on stderr with a non-zero
// exit status.

#include <algorithm>
#include <cassert>
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint32_t kLastCodePoint = 0x10FFFF;
constexpr std::size_t kMaxNameLength = 255;

[[noreturn]] void fail(const std::string_view file, const std::size_t line, const std::string_view message) {
    if (line == 0) {
        std::fprintf(stderr, "gen_ucd: %.*s: %.*s\n", static_cast<int>(file.size()), file.data(),
                     static_cast<int>(message.size()), message.data());
    } else {
        std::fprintf(stderr, "gen_ucd: %.*s:%zu: %.*s\n", static_cast<int>(file.size()), file.data(), line,
                     static_cast<int>(message.size()), message.data());
    }
    std::exit(1);
}

std::string hex(const std::uint32_t value) {
    char buffer[16];
    const auto [ptr, ec] = std::to_chars(buffer, buffer + sizeof buffer, value, 16);
    assert(ec == std::errc{});
    return "0x" + std::string(buffer, ptr);
}

struct SourceLine {
    std::size_t number = 0;
    std::string text;
};

std::vector<SourceLine> read_lines(const std::string &path) {
    std::ifstream in(path);
    if (!in) {
        fail(path, 0, "cannot open file for reading; check the path");
    }
    std::vector<SourceLine> lines;
    std::string text;
    std::size_t number = 0;
    while (std::getline(in, text)) {
        ++number;
        if (!text.empty() && text.back() == '\r') {
            text.pop_back();
        }
        lines.push_back(SourceLine{.number = number, .text = text});
    }
    if (in.bad()) {
        fail(path, 0, "read error");
    }
    return lines;
}

std::vector<std::string_view> split(const std::string_view text, const char separator) {
    std::vector<std::string_view> fields;
    std::size_t start = 0;
    for (;;) {
        const std::size_t end = text.find(separator, start);
        if (end == std::string_view::npos) {
            fields.push_back(text.substr(start));
            return fields;
        }
        fields.push_back(text.substr(start, end - start));
        start = end + 1;
    }
}

std::string_view trim(std::string_view text) {
    constexpr std::string_view blanks = " \t";
    const std::size_t first = text.find_first_not_of(blanks);
    if (first == std::string_view::npos) {
        return {};
    }
    text.remove_prefix(first);
    text.remove_suffix(text.size() - 1 - text.find_last_not_of(blanks));
    return text;
}

std::uint32_t parse_code_point(const std::string &file, const std::size_t line, const std::string_view text) {
    std::uint32_t value = 0;
    const char *const end = text.data() + text.size();
    const auto [ptr, ec] = std::from_chars(text.data(), end, value, 16);
    if (text.size() < 4 || text.size() > 6 || ec != std::errc{} || ptr != end || value > kLastCodePoint) {
        fail(file, line, "invalid code point '" + std::string(text) + "' (expected 4 to 6 hex digits, at most 10FFFF)");
    }
    return value;
}

// Category identifiers accepted in UnicodeData.txt; they are emitted as
// tellmeutf::ucd::Category::<code>.
const std::set<std::string_view> &known_categories() {
    static const std::set<std::string_view> categories{
        "Lu", "Ll", "Lt", "Lm", "Lo", "Mn", "Mc", "Me", "Nd", "Nl", "No", "Pc", "Pd", "Ps", "Pe",
        "Pi", "Pf", "Po", "Sm", "Sc", "Sk", "So", "Zs", "Zl", "Zp", "Cc", "Cf", "Cs", "Co", "Cn"};
    return categories;
}

// Maps the name of a First/Last range to a RangeRule identifier; an unknown
// name is rejected so that a new Unicode version must be taught explicitly.
std::string range_rule(const std::string &file, const std::size_t line, const std::string &name) {
    static const std::map<std::string, std::string> rules{
        {"Hangul Syllable", "HangulSyllable"},
        {"CJK Ideograph", "CjkIdeograph"},
        {"Tangut Ideograph", "TangutIdeograph"},
        {"Tangut Ideograph Supplement", "TangutIdeograph"},
        {"Jurchen Character", "JurchenCharacter"},
        {"Seal Character", "SealCharacter"},
        {"Non Private Use High Surrogate", "Surrogate"},
        {"Private Use High Surrogate", "Surrogate"},
        {"Low Surrogate", "Surrogate"},
        {"Private Use", "PrivateUse"},
        {"Plane 15 Private Use", "PrivateUse"},
        {"Plane 16 Private Use", "PrivateUse"},
    };
    constexpr std::string_view extension_prefix = "CJK Ideograph Extension ";
    if (name.size() == extension_prefix.size() + 1 && name.starts_with(extension_prefix) &&
        name.back() >= 'A' && name.back() <= 'Z') {
        return "CjkIdeograph";
    }
    const auto found = rules.find(name);
    if (found == rules.end()) {
        fail(file, line, "unknown First/Last range name '" + name +
                             "'; teach gen_ucd and src/ucd.cpp how to derive names for it");
    }
    return found->second;
}

struct Entry {
    std::uint32_t cp = 0;
    std::string category;
    std::string name;
};

struct Range {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
    std::string category;
    std::string rule;
};

struct Alias {
    std::uint32_t cp = 0;
    std::string type;  // RangeRule-style identifier, e.g. "Control"
    std::string name;
};

struct WideRange {
    std::uint32_t first = 0;
    std::uint32_t last = 0;
};

struct UnicodeData {
    std::vector<Entry> entries;
    std::vector<Range> ranges;
};

UnicodeData parse_unicode_data(const std::string &file) {
    struct UnicodeData data;
    // Open range state: set between a "First" line and its "Last" line.
    bool in_range = false;
    std::uint32_t range_first = 0;
    std::string range_name;
    std::size_t range_line = 0;
    std::string range_category;

    for (const struct SourceLine &source : read_lines(file)) {
        const std::size_t line = source.number;
        if (source.text.empty()) {
            continue;
        }
        const std::vector<std::string_view> fields = split(source.text, ';');
        if (fields.size() != 15) {
            fail(file, line, "expected 15 ';'-separated fields, found " + std::to_string(fields.size()));
        }
        const std::uint32_t cp = parse_code_point(file, line, fields[0]);
        const std::string name(fields[1]);
        const std::string category(fields[2]);
        if (!known_categories().contains(category)) {
            fail(file, line, "unknown general category '" + category + "'");
        }
        if (name.empty()) {
            fail(file, line, "empty name");
        }

        const bool is_marker = name.front() == '<' && name.back() == '>';
        const bool is_first = is_marker && name.ends_with(", First>");
        const bool is_last = is_marker && name.ends_with(", Last>");

        if (in_range) {
            if (!is_last) {
                fail(file, line, "expected the 'Last' line of range '" + range_name + "' opened at line " +
                                     std::to_string(range_line));
            }
            const std::string closing = name.substr(1, name.size() - 1 - std::string_view(", Last>").size());
            if (closing != range_name || category != range_category) {
                fail(file, line, "'Last' line does not match the 'First' line at line " + std::to_string(range_line));
            }
            if (cp < range_first) {
                fail(file, line, "range ends before it starts");
            }
            data.ranges.push_back(Range{.first = range_first,
                                        .last = cp,
                                        .category = category,
                                        .rule = range_rule(file, range_line, range_name)});
            in_range = false;
            continue;
        }
        if (is_last) {
            fail(file, line, "'Last' line without a preceding 'First' line");
        }
        if (is_first) {
            in_range = true;
            range_first = cp;
            range_line = line;
            range_category = category;
            range_name = name.substr(1, name.size() - 1 - std::string_view(", First>").size());
            continue;
        }
        if (name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -") != std::string::npos &&
            name != "<control>") {
            fail(file, line, "unexpected character in name '" + name + "'");
        }
        if (name.size() > kMaxNameLength) {
            fail(file, line, "name longer than " + std::to_string(kMaxNameLength) + " characters");
        }
        data.entries.push_back(Entry{.cp = cp, .category = category, .name = name});
    }
    if (in_range) {
        fail(file, range_line, "range '" + range_name + "' is never closed");
    }
    if (data.entries.empty()) {
        fail(file, 0, "no entries found");
    }

    // Order and overlap checks over entries and ranges together.
    for (std::size_t i = 1; i < data.entries.size(); ++i) {
        if (data.entries[i].cp <= data.entries[i - 1].cp) {
            fail(file, 0, "code points are not strictly ascending near " +
                              hex(data.entries[i].cp));
        }
    }
    std::sort(data.ranges.begin(), data.ranges.end(),
              [](const struct Range &a, const struct Range &b) { return a.first < b.first; });
    for (std::size_t i = 0; i < data.ranges.size(); ++i) {
        if (i > 0 && data.ranges[i].first <= data.ranges[i - 1].last) {
            fail(file, 0, "overlapping ranges");
        }
        const struct Range &range = data.ranges[i];
        if (range.rule == "HangulSyllable" && (range.first != 0xAC00 || range.last != 0xD7A3)) {
            fail(file, 0, "the Hangul Syllable range is " + hex(range.first) + ".." + hex(range.last) +
                              " but src/ucd.cpp derives syllable names for 0xAC00..0xD7A3 only");
        }
        const auto inside = std::lower_bound(data.entries.begin(), data.entries.end(), range.first,
                                             [](const struct Entry &e, const std::uint32_t cp) { return e.cp < cp; });
        if (inside != data.entries.end() && inside->cp <= range.last) {
            fail(file, 0, "an explicit entry lies inside a First/Last range (" + hex(inside->cp) + ")");
        }
    }
    return data;
}

// The first line of a UCD file reads "# <stem>-<version>.txt"; returns the
// version.
std::string header_version(const std::string &file, const std::vector<SourceLine> &lines,
                           const std::string_view stem) {
    if (lines.empty()) {
        fail(file, 0, "empty file");
    }
    const std::string header_prefix = "# " + std::string(stem) + "-";
    constexpr std::string_view header_suffix = ".txt";
    const std::string_view header = lines.front().text;
    if (!header.starts_with(header_prefix) || !header.ends_with(header_suffix) ||
        header.size() <= header_prefix.size() + header_suffix.size()) {
        fail(file, 1, "first line must look like '" + header_prefix + "<version>.txt'");
    }
    const std::string version(
        header.substr(header_prefix.size(), header.size() - header_prefix.size() - header_suffix.size()));
    if (version.find_first_not_of("0123456789.") != std::string::npos || version.front() == '.' ||
        version.back() == '.') {
        fail(file, 1, "unparsable Unicode version '" + version + "'");
    }
    return version;
}

struct AliasData {
    std::string version;
    std::vector<Alias> aliases;
};

AliasData parse_name_aliases(const std::string &file) {
    static const std::map<std::string_view, std::string> types{
        {"correction", "Correction"}, {"control", "Control"},         {"alternate", "Alternate"},
        {"figment", "Figment"},       {"abbreviation", "Abbreviation"},
    };
    const std::vector<SourceLine> lines = read_lines(file);
    struct AliasData data;
    data.version = header_version(file, lines, "NameAliases");

    for (const struct SourceLine &source : lines) {
        const std::string_view text = source.text;
        if (text.empty() || text.front() == '#') {
            continue;
        }
        const std::vector<std::string_view> fields = split(text, ';');
        if (fields.size() != 3) {
            fail(file, source.number, "expected 3 ';'-separated fields, found " + std::to_string(fields.size()));
        }
        const std::uint32_t cp = parse_code_point(file, source.number, fields[0]);
        const std::string name(trim(fields[1]));
        const std::string_view type_text = trim(fields[2]);
        const auto type = types.find(type_text);
        if (type == types.end()) {
            fail(file, source.number, "unknown alias type '" + std::string(type_text) + "'");
        }
        if (name.empty() || name.size() > kMaxNameLength ||
            name.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -") != std::string::npos) {
            fail(file, source.number, "invalid alias name '" + name + "'");
        }
        data.aliases.push_back(Alias{.cp = cp, .type = type->second, .name = name});
    }
    // Stable: aliases of one code point keep their file order.
    std::stable_sort(data.aliases.begin(), data.aliases.end(),
                     [](const struct Alias &a, const struct Alias &b) { return a.cp < b.cp; });
    return data;
}

std::vector<WideRange> parse_east_asian_width(const std::string &file, const std::string &expected_version) {
    const std::vector<SourceLine> lines = read_lines(file);
    if (const std::string version = header_version(file, lines, "EastAsianWidth"); version != expected_version) {
        fail(file, 1, "Unicode version " + version + " differs from the NameAliases.txt version " + expected_version);
    }
    std::vector<WideRange> ranges;
    for (const struct SourceLine &source : lines) {
        std::string_view text = source.text;
        if (const std::size_t hash = text.find('#'); hash != std::string_view::npos) {
            text = text.substr(0, hash);
        }
        text = trim(text);
        if (text.empty()) {
            continue;
        }
        const std::vector<std::string_view> fields = split(text, ';');
        if (fields.size() != 2) {
            fail(file, source.number, "expected 'code points; width'");
        }
        const std::string_view cps = trim(fields[0]);
        const std::string_view width = trim(fields[1]);
        static const std::set<std::string_view> widths{"A", "F", "H", "N", "Na", "W"};
        if (!widths.contains(width)) {
            fail(file, source.number, "unknown East Asian Width '" + std::string(width) + "'");
        }
        std::uint32_t first = 0;
        std::uint32_t last = 0;
        if (const std::size_t dots = cps.find(".."); dots != std::string_view::npos) {
            first = parse_code_point(file, source.number, cps.substr(0, dots));
            last = parse_code_point(file, source.number, cps.substr(dots + 2));
        } else {
            first = parse_code_point(file, source.number, cps);
            last = first;
        }
        if (last < first) {
            fail(file, source.number, "range ends before it starts");
        }
        if (width != "W" && width != "F") {
            continue;
        }
        if (!ranges.empty() && first <= ranges.back().last) {
            if (first < ranges.back().first) {
                fail(file, source.number, "ranges are not in ascending order");
            }
            fail(file, source.number, "overlapping ranges");
        }
        // Merge touching ranges.
        if (!ranges.empty() && first == ranges.back().last + 1) {
            ranges.back().last = last;
        } else {
            ranges.push_back(WideRange{.first = first, .last = last});
        }
    }
    if (ranges.empty()) {
        fail(file, 0, "no Wide or Fullwidth ranges found");
    }
    return ranges;
}

enum class AnnotationKind {
    ShortName,  // sorts before Keyword
    Keyword,
};

struct AnnotationRow {
    std::uint32_t cp = 0;
    AnnotationKind kind = AnnotationKind::Keyword;
    std::string text;  // normalized into name form
};

// Decodes strict UTF-8 (no overlong forms, surrogates or values above 10FFFF);
// returns nothing for malformed input.
std::optional<std::vector<std::uint32_t>> decode_utf8(const std::string_view text) {
    std::vector<std::uint32_t> points;
    std::size_t i = 0;
    while (i < text.size()) {
        const auto lead = static_cast<unsigned char>(text[i]);
        std::size_t extra = 0;
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        if (lead < 0x80) {
            value = lead;
        } else if (lead >= 0xC2 && lead <= 0xDF) {
            extra = 1;
            value = lead & 0x1Fu;
            minimum = 0x80;
        } else if (lead >= 0xE0 && lead <= 0xEF) {
            extra = 2;
            value = lead & 0x0Fu;
            minimum = 0x800;
        } else if (lead >= 0xF0 && lead <= 0xF4) {
            extra = 3;
            value = lead & 0x07u;
            minimum = 0x10000;
        } else {
            return std::nullopt;
        }
        if (extra != 0 && i + extra >= text.size()) {
            return std::nullopt;
        }
        for (std::size_t k = 1; k <= extra; ++k) {
            const auto continuation = static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0u) != 0x80u) {
                return std::nullopt;
            }
            value = (value << 6) | (continuation & 0x3Fu);
        }
        if (value < minimum || value > kLastCodePoint || (value >= 0xD800 && value <= 0xDFFF)) {
            return std::nullopt;
        }
        points.push_back(value);
        i += extra + 1;
    }
    return points;
}

// Latin letters with a diacritic that fold to a plain ASCII letter in annotation
// texts: the Latin-1 Supplement vowels, y, n and c, and the macron and caron vowels.
struct Fold {
    std::uint32_t from;
    char to;
};
constexpr Fold kLatinFolds[] = {
    // A with grave, acute, circumflex, tilde, diaeresis, ring above (upper, then lower case)
    {0xC0, 'A'}, {0xC1, 'A'}, {0xC2, 'A'}, {0xC3, 'A'}, {0xC4, 'A'}, {0xC5, 'A'},
    {0xE0, 'A'}, {0xE1, 'A'}, {0xE2, 'A'}, {0xE3, 'A'}, {0xE4, 'A'}, {0xE5, 'A'},
    // C with cedilla
    {0xC7, 'C'}, {0xE7, 'C'},
    // E with grave, acute, circumflex, diaeresis
    {0xC8, 'E'}, {0xC9, 'E'}, {0xCA, 'E'}, {0xCB, 'E'},
    {0xE8, 'E'}, {0xE9, 'E'}, {0xEA, 'E'}, {0xEB, 'E'},
    // I with grave, acute, circumflex, diaeresis
    {0xCC, 'I'}, {0xCD, 'I'}, {0xCE, 'I'}, {0xCF, 'I'},
    {0xEC, 'I'}, {0xED, 'I'}, {0xEE, 'I'}, {0xEF, 'I'},
    // N with tilde
    {0xD1, 'N'}, {0xF1, 'N'},
    // O with grave, acute, circumflex, tilde, diaeresis
    {0xD2, 'O'}, {0xD3, 'O'}, {0xD4, 'O'}, {0xD5, 'O'}, {0xD6, 'O'},
    {0xF2, 'O'}, {0xF3, 'O'}, {0xF4, 'O'}, {0xF5, 'O'}, {0xF6, 'O'},
    // U with grave, acute, circumflex, diaeresis
    {0xD9, 'U'}, {0xDA, 'U'}, {0xDB, 'U'}, {0xDC, 'U'},
    {0xF9, 'U'}, {0xFA, 'U'}, {0xFB, 'U'}, {0xFC, 'U'},
    // Y with acute; lower case y with diaeresis
    {0xDD, 'Y'}, {0xFD, 'Y'}, {0xFF, 'Y'},
    // A, E, I, O, U with macron (upper, then lower case)
    {0x100, 'A'}, {0x101, 'A'}, {0x112, 'E'}, {0x113, 'E'}, {0x12A, 'I'}, {0x12B, 'I'},
    {0x14C, 'O'}, {0x14D, 'O'}, {0x16A, 'U'}, {0x16B, 'U'},
    // I, O, U with caron (upper, then lower case)
    {0x1CF, 'I'}, {0x1D0, 'I'}, {0x1D1, 'O'}, {0x1D2, 'O'}, {0x1D3, 'U'}, {0x1D4, 'U'},
};

char fold_latin_letter(const std::uint32_t cp) {
    for (const struct Fold &fold : kLatinFolds) {
        if (fold.from == cp) {
            return fold.to;
        }
    }
    return '\0';
}

bool is_alphanumeric(const char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

// Rewrites an annotation text into name form: upper case ASCII letters, digits,
// single spaces and hyphens between alphanumerics. Apostrophes vanish, other
// ASCII punctuation becomes a space and accented Latin letters fold to ASCII.
// Returns nothing when the text holds any other non-ASCII character or has no
// word left.
std::optional<std::string> normalize_annotation(const std::vector<std::uint32_t> &points) {
    std::string raw;
    for (const std::uint32_t cp : points) {
        if (cp == '\'' || cp == 0x2019) {
            continue;
        }
        if (cp < 0x80) {
            const char c = static_cast<char>(cp);
            if (c >= 'a' && c <= 'z') {
                raw += static_cast<char>(c - 'a' + 'A');
            } else if (is_alphanumeric(c) || c == '-') {
                raw += c;
            } else {
                raw += ' ';
            }
        } else if (const char folded = fold_latin_letter(cp); folded != '\0') {
            raw += folded;
        } else {
            return std::nullopt;
        }
    }
    std::string text;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        const char c = raw[i];
        if (c == '-' && !(i > 0 && is_alphanumeric(raw[i - 1]) && i + 1 < raw.size() && is_alphanumeric(raw[i + 1]))) {
            text += ' ';
        } else {
            text += c;
        }
    }
    std::string collapsed;
    for (const char c : text) {
        if (c == ' ' && (collapsed.empty() || collapsed.back() == ' ')) {
            continue;
        }
        collapsed += c;
    }
    if (!collapsed.empty() && collapsed.back() == ' ') {
        collapsed.pop_back();
    }
    if (collapsed.empty()) {
        return std::nullopt;
    }
    return collapsed;
}

// Replaces the five predefined XML entities; any other '&' is malformed.
std::string decode_entities(const std::string &file, const std::size_t line, const std::string_view text) {
    static const std::map<std::string_view, char> entities{
        {"&amp;", '&'}, {"&lt;", '<'}, {"&gt;", '>'}, {"&quot;", '"'}, {"&apos;", '\''},
    };
    std::string decoded;
    for (std::size_t i = 0; i < text.size();) {
        if (text[i] != '&') {
            decoded += text[i++];
            continue;
        }
        const std::size_t semicolon = text.find(';', i);
        const auto found = semicolon == std::string_view::npos ? entities.end()
                                                               : entities.find(text.substr(i, semicolon - i + 1));
        if (found == entities.end()) {
            fail(file, line, "unknown or malformed XML entity in '" + std::string(text) + "'");
        }
        decoded += found->second;
        i = semicolon + 1;
    }
    return decoded;
}

// The en.xml annotation lines look like
//   <annotation cp="X">keyword | keyword</annotation>
//   <annotation cp="X" type="tts">short name</annotation>
// Only single-code-point annotations are kept; XML comments are ignored.
std::vector<AnnotationRow> parse_annotations(const std::string &file) {
    constexpr std::string_view open_tag = "<annotation ";
    constexpr std::string_view cp_attribute = "<annotation cp=\"";
    constexpr std::string_view tts_attribute = " type=\"tts\">";
    constexpr std::string_view close_tag = "</annotation>";

    struct Pending {
        AnnotationRow row;
        std::size_t line = 0;
    };
    std::vector<Pending> pending;
    std::set<std::uint32_t> short_name_cps;
    bool in_comment = false;

    for (const struct SourceLine &source : read_lines(file)) {
        const std::size_t line = source.number;
        // The text outside XML comments.
        std::string visible;
        for (std::size_t pos = 0; pos < source.text.size();) {
            if (in_comment) {
                const std::size_t end = source.text.find("-->", pos);
                if (end == std::string::npos) {
                    break;
                }
                in_comment = false;
                pos = end + 3;
            } else {
                const std::size_t start = source.text.find("<!--", pos);
                visible.append(source.text, pos, start == std::string::npos ? std::string::npos : start - pos);
                if (start == std::string::npos) {
                    break;
                }
                in_comment = true;
                pos = start + 4;
            }
        }
        const std::string_view text = trim(visible);
        if (text.find(open_tag) == std::string_view::npos) {
            continue;
        }

        if (!text.starts_with(cp_attribute) || !text.ends_with(close_tag)) {
            fail(file, line, "malformed annotation line; expected '<annotation cp=\"...\">...</annotation>'");
        }
        const std::string_view after_open = text.substr(cp_attribute.size());
        const std::size_t cp_end = after_open.find('"');
        if (cp_end == std::string_view::npos) {
            fail(file, line, "unterminated cp attribute");
        }
        std::string_view rest = after_open.substr(cp_end + 1);
        bool is_short_name = false;
        if (rest.starts_with(tts_attribute)) {
            is_short_name = true;
            rest.remove_prefix(tts_attribute.size());
        } else if (rest.starts_with('>')) {
            rest.remove_prefix(1);
        } else if (rest.starts_with(" type=")) {
            fail(file, line, "unsupported annotation type; only type=\"tts\" is understood");
        } else {
            fail(file, line, "malformed annotation line; expected '>' after the cp attribute");
        }
        rest.remove_suffix(close_tag.size());
        if (rest.find('<') != std::string_view::npos) {
            fail(file, line, "unexpected markup inside an annotation");
        }

        const std::optional<std::vector<std::uint32_t>> cp_points =
            decode_utf8(decode_entities(file, line, after_open.substr(0, cp_end)));
        if (!cp_points) {
            fail(file, line, "invalid UTF-8 in the cp attribute");
        }
        if (cp_points->empty()) {
            fail(file, line, "empty cp attribute");
        }
        if (cp_points->size() > 1) {
            continue;  // an emoji sequence
        }
        const std::uint32_t cp = cp_points->front();

        if (is_short_name && !short_name_cps.insert(cp).second) {
            fail(file, line, "second short name for code point " + hex(cp));
        }
        const std::string decoded = decode_entities(file, line, rest);
        const std::vector<std::string_view> pieces =
            is_short_name ? std::vector<std::string_view>{decoded} : [&] {
                std::vector<std::string_view> found;
                std::string_view remaining = decoded;
                for (std::size_t bar; (bar = remaining.find(" | ")) != std::string_view::npos;) {
                    found.push_back(remaining.substr(0, bar));
                    remaining.remove_prefix(bar + 3);
                }
                found.push_back(remaining);
                return found;
            }();
        for (const std::string_view piece : pieces) {
            const std::optional<std::vector<std::uint32_t>> points = decode_utf8(trim(piece));
            if (!points) {
                fail(file, line, "invalid UTF-8 in an annotation text");
            }
            std::optional<std::string> name = normalize_annotation(*points);
            if (!name) {
                continue;
            }
            if (name->size() > kMaxNameLength) {
                fail(file, line, "annotation longer than " + std::to_string(kMaxNameLength) + " characters");
            }
            pending.push_back(Pending{.row = AnnotationRow{.cp = cp,
                                                            .kind = is_short_name ? AnnotationKind::ShortName
                                                                                  : AnnotationKind::Keyword,
                                                            .text = std::move(*name)},
                                      .line = line});
        }
    }

    // Stable: keywords of one code point keep their file order.
    std::stable_sort(pending.begin(), pending.end(), [](const struct Pending &a, const struct Pending &b) {
        return a.row.cp != b.row.cp ? a.row.cp < b.row.cp : a.row.kind < b.row.kind;
    });
    // A text that repeats an earlier text of the same code point is dropped, so
    // the short name wins over an equal keyword.
    std::vector<AnnotationRow> rows;
    std::set<std::string> seen;
    for (struct Pending &item : pending) {
        if (rows.empty() || rows.back().cp != item.row.cp) {
            seen.clear();
        }
        if (seen.insert(item.row.text).second) {
            rows.push_back(std::move(item.row));
        }
    }
    if (rows.empty()) {
        fail(file, 0, "no annotations found");
    }
    return rows;
}

// Collects names into one blob and hands out (offset, length) references. The
// blob is emitted as a byte array: a string literal of this size exceeds the
// limit that C++ compilers are required to support.
class NameBlob {
public:
    std::string reference(const std::string &name) {
        assert(!name.empty() && name.size() <= kMaxNameLength);
        const std::size_t offset = bytes_.size();
        bytes_ += name;
        return "{" + std::to_string(offset) + "," + std::to_string(name.size()) + "}";
    }
    std::size_t size() const { return bytes_.size(); }
    // Comma-separated decimal byte values, 32 per line.
    std::string array_body() const {
        std::string body;
        for (std::size_t i = 0; i < bytes_.size(); ++i) {
            body += std::to_string(static_cast<unsigned>(static_cast<unsigned char>(bytes_[i])));
            body += (i + 1) % 32 == 0 ? ",\n" : ",";
        }
        return body;
    }

private:
    std::string bytes_;
};

std::string generate(const struct UnicodeData &unicode, const struct AliasData &aliases, const std::vector<WideRange> &wide,
                     const std::vector<AnnotationRow> &annotations) {
    NameBlob blob;
    std::ostringstream entries;
    for (const struct Entry &entry : unicode.entries) {
        entries << "{" << hex(entry.cp) << ",Category::" << entry.category << "," << blob.reference(entry.name)
                << "},\n";
    }
    std::ostringstream ranges;
    for (const struct Range &range : unicode.ranges) {
        ranges << "{" << hex(range.first) << "," << hex(range.last) << ",Category::" << range.category
               << ",RangeRule::" << range.rule << "},\n";
    }
    std::ostringstream alias_rows;
    for (const struct Alias &alias : aliases.aliases) {
        alias_rows << "{" << hex(alias.cp) << ",AliasType::" << alias.type << "," << blob.reference(alias.name)
                   << "},\n";
    }
    std::ostringstream wide_rows;
    for (const struct WideRange &range : wide) {
        wide_rows << "{" << hex(range.first) << "," << hex(range.last) << "},\n";
    }

    std::ostringstream annotation_rows;
    for (const struct AnnotationRow &annotation : annotations) {
        annotation_rows << "{" << hex(annotation.cp) << ",AnnotationKind::"
                        << (annotation.kind == AnnotationKind::ShortName ? "ShortName" : "Keyword") << ","
                        << blob.reference(annotation.text) << "},\n";
    }

    std::ostringstream out;
    out << "// Generated by tools/gen_ucd.cpp from the vendored UCD files and CLDR annotations. Do not edit.\n"
           "#include \"ucd_tables.hpp\"\n\n"
           "namespace tellmeutf::ucd::tables {\nnamespace {\n\n"
           "constexpr char kNames[] = {\n"
        << blob.array_body() << "\n};\n\n"
        << "constexpr Entry kEntries[] = {\n" << entries.str() << "};\n\n"
        << "constexpr Range kRanges[] = {\n" << ranges.str() << "};\n\n"
        << "constexpr Alias kAliases[] = {\n" << alias_rows.str() << "};\n\n"
        << "constexpr WideRange kWide[] = {\n" << wide_rows.str() << "};\n\n"
        << "constexpr Annotation kAnnotations[] = {\n" << annotation_rows.str() << "};\n\n"
        << "}  // namespace\n\n"
        << "static_assert(sizeof(kNames) == " << blob.size() << ");\n\n"
        << "constexpr Tables kTables{\n"
           "    .version = \"" << aliases.version << "\",\n"
           "    .names = std::string_view(kNames, " << blob.size() << "),\n"
           "    .entries = kEntries,\n"
           "    .ranges = kRanges,\n"
           "    .aliases = kAliases,\n"
           "    .wide = kWide,\n"
           "    .annotations = kAnnotations,\n"
           "};\n\n"
           "}  // namespace tellmeutf::ucd::tables\n";
    return out.str();
}

}  // namespace

int main(const int argc, const char *const argv[]) {
    std::vector<std::string> inputs;
    std::string output;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        if (arg == "-o") {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "gen_ucd: -o needs an output path\n");
                return 2;
            }
            output = argv[++i];
        } else {
            inputs.emplace_back(arg);
        }
    }
    if (inputs.size() != 4 || output.empty()) {
        std::fprintf(stderr,
                     "usage: gen_ucd <UnicodeData.txt> <NameAliases.txt> <EastAsianWidth.txt> <annotations/en.xml> "
                     "-o <output.cpp>\n");
        return 2;
    }

    const struct UnicodeData unicode = parse_unicode_data(inputs[0]);
    const struct AliasData aliases = parse_name_aliases(inputs[1]);
    const std::vector<WideRange> wide = parse_east_asian_width(inputs[2], aliases.version);
    const std::vector<AnnotationRow> annotations = parse_annotations(inputs[3]);

    // Every alias and annotation must refer to a code point that the other data
    // describes as an explicit entry or inside a range.
    const auto is_listed = [&unicode](const std::uint32_t cp) {
        const auto entry = std::lower_bound(unicode.entries.begin(), unicode.entries.end(), cp,
                                            [](const struct Entry &e, const std::uint32_t value) { return e.cp < value; });
        if (entry != unicode.entries.end() && entry->cp == cp) {
            return true;
        }
        return std::any_of(unicode.ranges.begin(), unicode.ranges.end(),
                           [cp](const struct Range &r) { return cp >= r.first && cp <= r.last; });
    };
    for (const struct Alias &alias : aliases.aliases) {
        if (!is_listed(alias.cp)) {
            fail(inputs[1], 0, "alias '" + alias.name + "' refers to a code point (" +
                                   hex(alias.cp) + ") that UnicodeData.txt does not list");
        }
    }
    for (const struct AnnotationRow &annotation : annotations) {
        if (!is_listed(annotation.cp)) {
            fail(inputs[3], 0, "annotation '" + annotation.text + "' refers to a code point (" + hex(annotation.cp) +
                                   ") that UnicodeData.txt does not list; the CLDR release is newer than the Unicode data");
        }
    }

    const std::string text = generate(unicode, aliases, wide, annotations);
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        fail(output, 0, "cannot write the output file");
    }
    return 0;
}
