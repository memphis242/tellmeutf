#include "query.hpp"

#include "utf8.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <string_view>
#include <utility>

namespace tellmeutf::query {

namespace {

constexpr char32_t kLastCodePoint = 0x10FFFF;
constexpr std::size_t kMaxPrefixedDigits = 8;

enum class ArgumentKind { Text, Character, PrefixedCodePoint, BareCodePoint, Word };

bool is_ascii(const unsigned char byte) { return byte < 0x80; }

// Value of a hex digit, or -1.
int hex_value(const char c) {
    switch (c) {
    case '0': case '1': case '2': case '3': case '4':
    case '5': case '6': case '7': case '8': case '9':
        return c - '0';
    case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
        return c - 'a' + 10;
    case 'A': case 'B': case 'C': case 'D': case 'E': case 'F':
        return c - 'A' + 10;
    default:
        return -1;
    }
}

bool all_hex_digits(const std::string_view text) {
    return !text.empty() &&
           std::ranges::all_of(text, [](const char c) { return hex_value(c) >= 0; });
}

// Length of the recognized code point prefix at the start of `arg`, or 0.
std::size_t prefix_length(const std::string_view arg) {
    if (arg.size() < 2) {
        return 0;
    }
    const std::string_view head = arg.substr(0, 2);
    constexpr std::string_view kPrefixes[] = {"U+", "u+", "0x", "0X", "\\u", "\\U"};
    return std::ranges::find(kPrefixes, head) != std::end(kPrefixes) ? 2 : 0;
}

ArgumentKind classify(const std::string_view arg) {
    if (std::ranges::any_of(arg, [](const char c) { return !is_ascii(static_cast<unsigned char>(c)); })) {
        return ArgumentKind::Text;
    }
    if (arg.size() == 1) {
        return ArgumentKind::Character;
    }
    if (prefix_length(arg) != 0) {
        return ArgumentKind::PrefixedCodePoint;
    }
    if (arg.size() >= 2 && all_hex_digits(arg)) {
        return ArgumentKind::BareCodePoint;
    }
    return ArgumentKind::Word;
}

// Value of a string of hex digits, saturated just above U+10FFFF.
char32_t hex_to_code_point(const std::string_view digits) {
    assert(all_hex_digits(digits));
    unsigned long long value = 0;
    for (const char c : digits) {
        value = value * 16 + static_cast<unsigned>(hex_value(c));
        if (value > kLastCodePoint) {
            return kLastCodePoint + 1;
        }
    }
    return static_cast<char32_t>(value);
}

std::expected<char32_t, QueryError> parse_code_point(const std::string_view arg, const std::string_view digits,
                                                     const bool prefixed) {
    if (prefixed && (digits.empty() || digits.size() > kMaxPrefixedDigits || !all_hex_digits(digits))) {
        return std::unexpected(QueryError{
            "'" + utf8::escape_for_message(arg) + "' has a code point prefix but is not followed by 1 to 8 hex digits; "
            "write for example U+276F, or use --name to look it up as a name"});
    }
    const char32_t cp = hex_to_code_point(digits);
    if (cp > kLastCodePoint) {
        return std::unexpected(QueryError{
            "'" + utf8::escape_for_message(arg) + "' is above U+10FFFF, the last Unicode code point; use a smaller value"});
    }
    return cp;
}

Lookup code_point_lookup(const char32_t cp) {
    return Lookup{.kind = Lookup::Kind::CodePoint, .code_point = cp, .name = {}};
}

std::expected<std::vector<char32_t>, QueryError> decode_text(const std::string &arg) {
    auto decoded = utf8::decode(arg);
    if (!decoded) {
        return std::unexpected(QueryError{
            "argument is not valid UTF-8 (" + decoded.error().reason + " at byte " +
            std::to_string(decoded.error().offset) + "); pass a valid character, a code point, or a name"});
    }
    return std::move(*decoded);
}

std::string join_with_spaces(const std::vector<std::string> &arguments) {
    std::string joined;
    for (const std::string &arg : arguments) {
        if (!joined.empty()) {
            joined.push_back(' ');
        }
        joined += arg;
    }
    return joined;
}

}  // namespace

std::expected<std::vector<Lookup>, QueryError>
interpret(const std::vector<std::string> &arguments, const bool force_name) {
    std::vector<ArgumentKind> kinds;
    kinds.reserve(arguments.size());
    for (const std::string &arg : arguments) {
        kinds.push_back(classify(arg));
    }

    const bool as_name =
        force_name || std::ranges::find(kinds, ArgumentKind::Word) != kinds.end();
    if (as_name) {
        // Text must stay valid UTF-8, and a code point prefix must stay well
        // formed, even though the argument only joins the name. --name reads
        // prefixed arguments as plain name text.
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            if (kinds[i] == ArgumentKind::Text) {
                if (auto decoded = decode_text(arguments[i]); !decoded) {
                    return std::unexpected(decoded.error());
                }
            } else if (kinds[i] == ArgumentKind::PrefixedCodePoint && !force_name) {
                if (auto cp = parse_code_point(arguments[i], std::string_view(arguments[i]).substr(prefix_length(arguments[i])), true);
                    !cp) {
                    return std::unexpected(cp.error());
                }
            }
        }
        return std::vector<Lookup>{
            Lookup{.kind = Lookup::Kind::Name, .code_point = 0, .name = join_with_spaces(arguments)}};
    }

    std::vector<Lookup> lookups;
    for (std::size_t i = 0; i < arguments.size(); ++i) {
        const std::string &arg = arguments[i];
        switch (kinds[i]) {
        case ArgumentKind::Text: {
            auto decoded = decode_text(arg);
            if (!decoded) {
                return std::unexpected(decoded.error());
            }
            for (const char32_t cp : *decoded) {
                lookups.push_back(code_point_lookup(cp));
            }
            break;
        }
        case ArgumentKind::Character:
            lookups.push_back(code_point_lookup(static_cast<char32_t>(static_cast<unsigned char>(arg[0]))));
            break;
        case ArgumentKind::PrefixedCodePoint:
        case ArgumentKind::BareCodePoint: {
            const bool prefixed = kinds[i] == ArgumentKind::PrefixedCodePoint;
            const std::string_view digits = std::string_view(arg).substr(prefixed ? prefix_length(arg) : 0);
            auto cp = parse_code_point(arg, digits, prefixed);
            if (!cp) {
                return std::unexpected(cp.error());
            }
            lookups.push_back(code_point_lookup(*cp));
            break;
        }
        case ArgumentKind::Word:
            assert(false && "words are handled as a name lookup");
            break;
        }
    }
    return lookups;
}

}  // namespace tellmeutf::query
