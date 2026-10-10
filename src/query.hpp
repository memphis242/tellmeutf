// Interpretation of command-line arguments as lookups (see docs/design.md,
// "Argument interpretation"). Purely syntactic: never consults the Unicode data.
#pragma once

#include <expected>
#include <string>
#include <vector>

namespace tellmeutf::query {

struct Lookup {
    enum class Kind { CodePoint, Name };
    Kind kind = Kind::CodePoint;
    char32_t code_point = 0;  // meaningful for Kind::CodePoint; at most U+10FFFF
    std::string name;         // meaningful for Kind::Name; words joined by single spaces
};

struct QueryError {
    // Plain message that says what is wrong and what to do about it.
    std::string message;
};

// Turns the positional arguments into lookups, in order. With `force_name`, or
// when any argument is a word, all arguments form one name lookup.
[[nodiscard]] std::expected<std::vector<Lookup>, QueryError>
interpret(const std::vector<std::string> &arguments, bool force_name);

}  // namespace tellmeutf::query
