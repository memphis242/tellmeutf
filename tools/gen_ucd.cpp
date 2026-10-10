// Build-time generator: reads the vendored Unicode Character Database files and
// writes one C++ source file holding the lookup tables declared in
// src/ucd_tables.hpp.
//
//   gen_ucd <UnicodeData.txt> <NameAliases.txt> <EastAsianWidth.txt> -o <output.cpp>
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
    UnicodeData data;
    // Open range state: set between a "First" line and its "Last" line.
    bool in_range = false;
    std::uint32_t range_first = 0;
    std::string range_name;
    std::size_t range_line = 0;
    std::string range_category;

    for (const SourceLine &source : read_lines(file)) {
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
              [](const Range &a, const Range &b) { return a.first < b.first; });
    for (std::size_t i = 0; i < data.ranges.size(); ++i) {
        if (i > 0 && data.ranges[i].first <= data.ranges[i - 1].last) {
            fail(file, 0, "overlapping ranges");
        }
        const Range &range = data.ranges[i];
        const auto inside = std::lower_bound(data.entries.begin(), data.entries.end(), range.first,
                                             [](const Entry &e, const std::uint32_t cp) { return e.cp < cp; });
        if (inside != data.entries.end() && inside->cp <= range.last) {
            fail(file, 0, "an explicit entry lies inside a First/Last range (" + hex(inside->cp) + ")");
        }
    }
    return data;
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
    if (lines.empty()) {
        fail(file, 0, "empty file");
    }

    // The first line is "# NameAliases-<version>.txt".
    constexpr std::string_view header_prefix = "# NameAliases-";
    constexpr std::string_view header_suffix = ".txt";
    const std::string_view header = lines.front().text;
    if (!header.starts_with(header_prefix) || !header.ends_with(header_suffix) ||
        header.size() <= header_prefix.size() + header_suffix.size()) {
        fail(file, 1, "first line must look like '# NameAliases-<version>.txt'");
    }
    AliasData data;
    data.version = std::string(
        header.substr(header_prefix.size(), header.size() - header_prefix.size() - header_suffix.size()));
    if (data.version.find_first_not_of("0123456789.") != std::string::npos || data.version.front() == '.' ||
        data.version.back() == '.') {
        fail(file, 1, "unparsable Unicode version '" + data.version + "'");
    }

    for (const SourceLine &source : lines) {
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
                     [](const Alias &a, const Alias &b) { return a.cp < b.cp; });
    return data;
}

std::vector<WideRange> parse_east_asian_width(const std::string &file) {
    std::vector<WideRange> ranges;
    for (const SourceLine &source : read_lines(file)) {
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

// Collects names into one blob and hands out (offset, length) references.
class NameBlob {
public:
    std::string reference(const std::string &name) {
        assert(!name.empty() && name.size() <= kMaxNameLength);
        const std::size_t offset = text_.size();
        text_ += name;
        literals_ += "\"" + name + "\"\n";
        return "{" + std::to_string(offset) + "," + std::to_string(name.size()) + "}";
    }
    // Names contain only [A-Z0-9 -] (validated while parsing), so the literals
    // need no escapes.
    const std::string &literals() const { return literals_; }
    std::size_t size() const { return text_.size(); }

private:
    std::string text_;
    std::string literals_;
};

std::string generate(const UnicodeData &unicode, const AliasData &aliases, const std::vector<WideRange> &wide) {
    NameBlob blob;
    std::ostringstream entries;
    for (const Entry &entry : unicode.entries) {
        entries << "{" << hex(entry.cp) << ",Category::" << entry.category << "," << blob.reference(entry.name)
                << "},\n";
    }
    std::ostringstream ranges;
    for (const Range &range : unicode.ranges) {
        ranges << "{" << hex(range.first) << "," << hex(range.last) << ",Category::" << range.category
               << ",RangeRule::" << range.rule << "},\n";
    }
    std::ostringstream alias_rows;
    for (const Alias &alias : aliases.aliases) {
        alias_rows << "{" << hex(alias.cp) << ",AliasType::" << alias.type << "," << blob.reference(alias.name)
                   << "},\n";
    }
    std::ostringstream wide_rows;
    for (const WideRange &range : wide) {
        wide_rows << "{" << hex(range.first) << "," << hex(range.last) << "},\n";
    }

    std::ostringstream out;
    out << "// Generated by tools/gen_ucd.cpp from the vendored UCD files. Do not edit.\n"
           "#include \"ucd_tables.hpp\"\n\n"
           "namespace tellmeutf::ucd::tables {\nnamespace {\n\n"
           "constexpr char kNames[] =\n"
        << blob.literals() << ";\n\n"
        << "constexpr Entry kEntries[] = {\n" << entries.str() << "};\n\n"
        << "constexpr Range kRanges[] = {\n" << ranges.str() << "};\n\n"
        << "constexpr Alias kAliases[] = {\n" << alias_rows.str() << "};\n\n"
        << "constexpr WideRange kWide[] = {\n" << wide_rows.str() << "};\n\n"
        << "}  // namespace\n\n"
        << "static_assert(sizeof(kNames) == " << blob.size() << " + 1);\n\n"
        << "constexpr Tables kTables{\n"
           "    .version = \"" << aliases.version << "\",\n"
           "    .names = std::string_view(kNames, " << blob.size() << "),\n"
           "    .entries = kEntries,\n"
           "    .ranges = kRanges,\n"
           "    .aliases = kAliases,\n"
           "    .wide = kWide,\n"
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
    if (inputs.size() != 3 || output.empty()) {
        std::fprintf(stderr,
                     "usage: gen_ucd <UnicodeData.txt> <NameAliases.txt> <EastAsianWidth.txt> -o <output.cpp>\n");
        return 2;
    }

    const UnicodeData unicode = parse_unicode_data(inputs[0]);
    const AliasData aliases = parse_name_aliases(inputs[1]);
    const std::vector<WideRange> wide = parse_east_asian_width(inputs[2]);

    // Every alias must refer to a code point that the other data describes as
    // an explicit entry or inside a range.
    for (const Alias &alias : aliases.aliases) {
        const auto entry = std::lower_bound(unicode.entries.begin(), unicode.entries.end(), alias.cp,
                                            [](const Entry &e, const std::uint32_t cp) { return e.cp < cp; });
        const bool listed = entry != unicode.entries.end() && entry->cp == alias.cp;
        const bool in_range = std::any_of(unicode.ranges.begin(), unicode.ranges.end(), [&](const Range &r) {
            return alias.cp >= r.first && alias.cp <= r.last;
        });
        if (!listed && !in_range) {
            fail(inputs[1], 0, "alias '" + alias.name + "' refers to a code point (" +
                                   hex(alias.cp) + ") that UnicodeData.txt does not list");
        }
    }

    const std::string text = generate(unicode, aliases, wide);
    std::ofstream out(output, std::ios::binary | std::ios::trunc);
    out << text;
    out.close();
    if (!out) {
        fail(output, 0, "cannot write the output file");
    }
    return 0;
}
