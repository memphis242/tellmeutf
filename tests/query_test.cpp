#include "check.hpp"
#include "query.hpp"

#include <string>
#include <vector>

namespace query = tellmeutf::query;

namespace {

using Args = std::vector<std::string>;

// The code points of an all-code-point result, or an empty vector (with a failed
// check) when the result is not that.
std::vector<char32_t> code_points(const Args &args, const bool force_name = false) {
    const auto result = query::interpret(args, force_name);
    CHECK(result.has_value());
    std::vector<char32_t> out;
    if (result) {
        for (const query::Lookup &lookup : *result) {
            CHECK(lookup.kind == query::Lookup::Kind::CodePoint);
            out.push_back(lookup.code_point);
        }
    }
    return out;
}

// The single name of a name result.
std::string name_of(const Args &args, const bool force_name = false) {
    const auto result = query::interpret(args, force_name);
    CHECK(result.has_value());
    if (result && result->size() == 1) {
        CHECK(result->front().kind == query::Lookup::Kind::Name);
        return result->front().name;
    }
    CHECK(false);
    return {};
}

bool is_error(const Args &args, const bool force_name = false) {
    const auto result = query::interpret(args, force_name);
    CHECK(result.has_value() || !result.error().message.empty());
    return !result.has_value();
}

std::string utf8_bytes(const std::vector<unsigned> &values) {
    std::string out;
    for (const unsigned v : values) {
        out.push_back(static_cast<char>(v));
    }
    return out;
}

}  // namespace

int main() {
    using CPs = std::vector<char32_t>;

    // Single ASCII characters, including ones that look like hex or options.
    CHECK(code_points({"a"}) == CPs{0x61});
    CHECK(code_points({"1"}) == CPs{0x31});
    CHECK(code_points({"-"}) == CPs{0x2D});
    CHECK(code_points({" "}) == CPs{0x20});

    // Non-ASCII text: every code point is its own lookup.
    CHECK(code_points({utf8_bytes({0xE2, 0x9D, 0xAF})}) == CPs{0x276F});
    CHECK(code_points({utf8_bytes({'e', 0xCC, 0x81})}) == (CPs{0x65, 0x301}));
    CHECK(code_points({utf8_bytes({0xF0, 0x9F, 0x98, 0x80})}) == CPs{0x1F600});
    CHECK(code_points({"a", utf8_bytes({0xC3, 0xA9})}) == (CPs{0x61, 0xE9}));

    // Prefixed code points.
    CHECK(code_points({"U+276F"}) == CPs{0x276F});
    CHECK(code_points({"u+276f"}) == CPs{0x276F});
    CHECK(code_points({"0x276f"}) == CPs{0x276F});
    CHECK(code_points({"0X276F"}) == CPs{0x276F});
    CHECK(code_points({"\\u276f"}) == CPs{0x276F});
    CHECK(code_points({"\\U0001F600"}) == CPs{0x1F600});
    CHECK(code_points({"U+0"}) == CPs{0});
    CHECK(code_points({"U+10FFFF"}) == CPs{0x10FFFF});
    CHECK(code_points({"U+00000041"}) == CPs{0x41});
    CHECK(code_points({"U+D800"}) == CPs{0xD800});

    // Bare hex digits are always a number.
    CHECK(code_points({"276f"}) == CPs{0x276F});
    CHECK(code_points({"a9"}) == CPs{0xA9});
    CHECK(code_points({"FF"}) == CPs{0xFF});
    CHECK(code_points({"face"}) == CPs{0xFACE});
    CHECK(code_points({"bed"}) == CPs{0xBED});
    CHECK(code_points({"00000041"}) == CPs{0x41});
    CHECK(code_points({"10ffff"}) == CPs{0x10FFFF});

    // Several arguments are processed in order.
    CHECK(code_points({"U+41", "42", "c"}) == (CPs{0x41, 0x42, 0x63}));

    // Out of range or malformed code points are errors, never names.
    CHECK(is_error({"U+110000"}));
    CHECK(is_error({"110000"}));
    CHECK(is_error({"ffffffffffffffffffff"}));
    CHECK(is_error({"U+"}));
    CHECK(is_error({"0x"}));
    CHECK(is_error({"U+12G4"}));
    CHECK(is_error({"0xzebra"}));
    CHECK(is_error({"U+123456789"}));
    CHECK(is_error({"\\u"}));
    CHECK(is_error({"U+41", "U+110000"}));
    CHECK(is_error({"U+zz", "snowman"}));  // a bad prefix is an error even next to a word

    // Invalid UTF-8 is an error.
    CHECK(is_error({utf8_bytes({0xFF})}));
    CHECK(is_error({utf8_bytes({0xC0, 0x80})}));
    CHECK(is_error({utf8_bytes({0xED, 0xA0, 0x80})}));
    CHECK(is_error({utf8_bytes({0xE2, 0x9D})}));
    CHECK(is_error({utf8_bytes({0xFF})}, true));  // also when joining a name

    // Words make one name lookup from all arguments.
    CHECK(name_of({"snowman"}) == "snowman");
    CHECK(name_of({"right-pointing"}) == "right-pointing");
    CHECK(name_of({"check mark"}) == "check mark");
    CHECK(name_of({"heavy", "check", "mark"}) == "heavy check mark");
    CHECK(name_of({"U+276F", "heavy"}) == "U+276F heavy");
    CHECK(name_of({"a", "b", "snowman"}) == "a b snowman");

    // --name reads everything as a name, even hex-looking input.
    CHECK(name_of({"face"}, true) == "face");
    CHECK(name_of({"FF"}, true) == "FF");
    CHECK(name_of({"a"}, true) == "a");
    CHECK(name_of({"U+276F"}, true) == "U+276F");

    // No arguments: nothing to look up.
    {
        const auto result = query::interpret({}, false);
        CHECK(result.has_value() && result->empty());
    }

    return tellmeutf::test::finish();
}
