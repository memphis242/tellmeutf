// Unicode Character Database queries: names, general categories, display widths.
//
// All data is compiled in (generated from the vendored UCD files by
// tools/gen_ucd.cpp); no function here touches the file system.
#pragma once

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
// underscores and medial hyphens are not significant; see docs/design.md,
// "Name matching"). Ascending code point order. Empty when nothing matches or
// when `name` has no words.
[[nodiscard]] std::vector<char32_t> find_exact(std::string_view name);

// Code points for which every word of `query` is a prefix of some word of one
// of the code point's names or aliases, best match first (see docs/design.md,
// "Name matching"). Each code point appears once. Empty when nothing matches or
// when `query` has no words. Does not enumerate the ideograph ranges.
[[nodiscard]] std::vector<char32_t> search(std::string_view query);

// Version of the compiled-in data, for example "18.0.0".
[[nodiscard]] std::string_view unicode_version();

}  // namespace tellmeutf::ucd
