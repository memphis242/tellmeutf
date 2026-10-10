// Internal: the shape of the tables emitted by tools/gen_ucd.cpp, shared by the
// generated source file and ucd.cpp. Not part of the public contract.
#pragma once

#include "ucd.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace tellmeutf::ucd::tables {

// A name in the generated blob: bytes [offset, offset + length).
struct NameRef {
    std::uint32_t offset = 0;
    std::uint8_t length = 0;
};

// An explicitly listed code point. Entries are sorted by `cp`, strictly ascending.
struct Entry {
    char32_t cp = 0;
    Category category = Category::Cn;
    NameRef name;
};

// How the names of a `First`/`Last` range are derived.
enum class RangeRule : std::uint8_t {
    HangulSyllable,
    CjkIdeograph,
    TangutIdeograph,
    JurchenCharacter,
    SealCharacter,
    Surrogate,
    PrivateUse,
};

// An inclusive code point range. Ranges are sorted and do not overlap each
// other or any entry.
struct Range {
    char32_t first = 0;
    char32_t last = 0;
    Category category = Category::Cn;
    RangeRule rule = RangeRule::PrivateUse;
};

enum class AliasType : std::uint8_t {
    Correction,
    Control,
    Alternate,
    Figment,
    Abbreviation,
};

// A name alias. Aliases are sorted by `cp`; aliases of one code point keep the
// order of NameAliases.txt.
struct Alias {
    char32_t cp = 0;
    AliasType type = AliasType::Correction;
    NameRef name;
};

// An inclusive range of East Asian Wide or Fullwidth code points. Sorted,
// non-overlapping.
struct WideRange {
    char32_t first = 0;
    char32_t last = 0;
};

struct Tables {
    std::string_view version;
    std::string_view names;  // blob that every NameRef points into
    std::span<const Entry> entries;
    std::span<const Range> ranges;
    std::span<const Alias> aliases;
    std::span<const WideRange> wide;
};

extern const Tables kTables;

}  // namespace tellmeutf::ucd::tables
