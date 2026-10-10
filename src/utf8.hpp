// Strict UTF-8 decoding and encoding.
#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace tellmeutf::utf8 {

struct DecodeError {
    // Byte offset of the first offending byte.
    std::size_t offset = 0;
    // Plain description, for example "overlong encoding".
    std::string reason;
};

// Decodes `text` into code points. Rejects stray continuation bytes, overlong
// forms, surrogates, values above U+10FFFF and truncated sequences.
[[nodiscard]] std::expected<std::vector<char32_t>, DecodeError> decode(std::string_view text);

// Appends the UTF-8 form of `cp`. Precondition: `cp` is a Unicode scalar value
// (at most U+10FFFF and not a surrogate).
void append(std::string &out, char32_t cp);

// UTF-8 form of `cp`; same precondition as append().
[[nodiscard]] std::string encode(char32_t cp);

}  // namespace tellmeutf::utf8
