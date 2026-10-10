#include "check.hpp"
#include "utf8.hpp"

#include <string>
#include <vector>

namespace utf8 = tellmeutf::utf8;

namespace {

// Builds a string from raw byte values so tests need no non-ASCII source text.
std::string bytes(const std::vector<unsigned> &values) {
    std::string out;
    for (const unsigned v : values) {
        out.push_back(static_cast<char>(v));
    }
    return out;
}

void check_roundtrip(const char32_t cp) {
    const std::string encoded = utf8::encode(cp);
    const auto decoded = utf8::decode(encoded);
    CHECK(decoded.has_value());
    if (decoded) {
        CHECK(decoded->size() == 1);
        CHECK(decoded->front() == cp);
    }
}

void check_rejected(const std::string &text, const std::size_t offset) {
    const auto decoded = utf8::decode(text);
    CHECK(!decoded.has_value());
    if (!decoded) {
        CHECK(decoded.error().offset == offset);
        CHECK(!decoded.error().reason.empty());
    }
}

}  // namespace

int main() {
    // Encoding lengths and known byte sequences.
    CHECK(utf8::encode(U'A') == "A");
    CHECK(utf8::encode(0x7F) == bytes({0x7F}));
    CHECK(utf8::encode(0x80) == bytes({0xC2, 0x80}));
    CHECK(utf8::encode(0x7FF) == bytes({0xDF, 0xBF}));
    CHECK(utf8::encode(0x800) == bytes({0xE0, 0xA0, 0x80}));
    CHECK(utf8::encode(0x276F) == bytes({0xE2, 0x9D, 0xAF}));
    CHECK(utf8::encode(0xFFFF) == bytes({0xEF, 0xBF, 0xBF}));
    CHECK(utf8::encode(0x10000) == bytes({0xF0, 0x90, 0x80, 0x80}));
    CHECK(utf8::encode(0x10FFFF) == bytes({0xF4, 0x8F, 0xBF, 0xBF}));
    CHECK(utf8::encode(0) == std::string(1, '\0'));

    // Round trip across encoding boundaries, including the edges around the surrogates.
    for (const char32_t cp : {U'\0', U'\x7F', U'\x80', U'\u07FF', U'\u0800', U'\uD7FF', U'\uE000', U'\uFFFF', U'\U00010000', U'\U0010FFFF'}) {
        check_roundtrip(cp);
    }

    // Decoding a mixed string yields every code point in order.
    {
        const auto decoded = utf8::decode(std::string("e") + bytes({0xCC, 0x81}) + bytes({0xF0, 0x9F, 0x98, 0x80}));
        CHECK(decoded.has_value());
        if (decoded) {
            CHECK((*decoded == std::vector<char32_t>{0x65, 0x301, 0x1F600}));
        }
    }
    {
        const auto decoded = utf8::decode("");
        CHECK(decoded.has_value() && decoded->empty());
    }
    {
        const auto decoded = utf8::decode(std::string(1, '\0'));
        CHECK(decoded.has_value() && decoded->size() == 1);
    }

    // Invalid input reports the offset of the offending sequence.
    check_rejected(bytes({0x80}), 0);                    // stray continuation
    check_rejected(bytes({'a', 0xBF}), 1);               // stray continuation after text
    check_rejected(bytes({0xC0, 0x80}), 0);              // overlong NUL
    check_rejected(bytes({0xC1, 0xBF}), 0);              // overlong
    check_rejected(bytes({0xE0, 0x80, 0x80}), 0);        // overlong 3-byte
    check_rejected(bytes({0xF0, 0x80, 0x80, 0x80}), 0);  // overlong 4-byte
    check_rejected(bytes({0xED, 0xA0, 0x80}), 0);        // U+D800
    check_rejected(bytes({0xED, 0xBF, 0xBF}), 0);        // U+DFFF
    check_rejected(bytes({0xF4, 0x90, 0x80, 0x80}), 0);  // U+110000
    check_rejected(bytes({0xF5, 0x80, 0x80, 0x80}), 0);  // lead byte above F4
    check_rejected(bytes({0xFF}), 0);
    check_rejected(bytes({0xFE}), 0);
    check_rejected(bytes({0xE2, 0x9D}), 0);              // truncated at end
    check_rejected(bytes({'x', 0xF0, 0x9F, 0x98}), 1);   // truncated at end after text
    check_rejected(bytes({0xE2, 'a', 0xAF}), 0);         // continuation replaced
    check_rejected(bytes({0xC2}), 0);

    return tellmeutf::test::finish();
}
