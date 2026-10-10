#include "utf8.hpp"

#include <array>
#include <cassert>
#include <cstdint>

namespace tellmeutf::utf8 {

namespace {

constexpr char32_t kMaxCodePoint = 0x10FFFF;

constexpr bool is_surrogate(const char32_t cp) { return cp >= 0xD800 && cp <= 0xDFFF; }

constexpr bool is_continuation(const unsigned char byte) { return (byte & 0xC0U) == 0x80U; }

// Shape of a multi-byte sequence, selected by its lead byte.
struct Lead {
    int length = 0;                  // total bytes; 0 means the byte cannot start a sequence
    unsigned char payload_mask = 0;  // lead-byte bits that carry code point bits
    unsigned char second_min = 0x80; // allowed range of the second byte
    unsigned char second_max = 0xBF;
    const char *second_reason = "invalid continuation byte";  // why a second byte below the range fails
    const char *second_reason_high = "invalid continuation byte";  // why a second byte above the range fails
};

Lead lead_for(const unsigned char byte) {
    switch (byte) {
    case 0xE0:
        return {3, 0x0F, 0xA0, 0xBF, "overlong encoding", "invalid continuation byte"};
    case 0xED:
        return {3, 0x0F, 0x80, 0x9F, "invalid continuation byte", "surrogate code point"};
    case 0xF0:
        return {4, 0x07, 0x90, 0xBF, "overlong encoding", "invalid continuation byte"};
    case 0xF4:
        return {4, 0x07, 0x80, 0x8F, "invalid continuation byte", "code point above U+10FFFF"};
    default:
        break;
    }
    if (byte >= 0xC2 && byte <= 0xDF) {
        return {2, 0x1F};
    }
    if (byte >= 0xE1 && byte <= 0xEF) {
        return {3, 0x0F};
    }
    if (byte >= 0xF1 && byte <= 0xF3) {
        return {4, 0x07};
    }
    return {};
}

DecodeError error_at(const std::size_t offset, std::string reason) {
    return DecodeError{.offset = offset, .reason = std::move(reason)};
}

}  // namespace

std::expected<std::vector<char32_t>, DecodeError> decode(const std::string_view text) {
    std::vector<char32_t> out;
    out.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        const auto first = static_cast<unsigned char>(text[i]);
        if (first < 0x80) {
            out.push_back(first);
            ++i;
            continue;
        }
        if (is_continuation(first)) {
            return std::unexpected(error_at(i, "continuation byte without a lead byte"));
        }
        if (first == 0xC0 || first == 0xC1) {
            return std::unexpected(error_at(i, "overlong encoding"));
        }
        const struct Lead lead = lead_for(first);
        if (lead.length == 0) {
            return std::unexpected(error_at(i, "byte that never occurs in UTF-8"));
        }
        if (i + 1 >= text.size()) {
            return std::unexpected(error_at(i, "truncated sequence"));
        }
        const auto second = static_cast<unsigned char>(text[i + 1]);
        if (!is_continuation(second)) {
            return std::unexpected(error_at(i, "truncated sequence"));
        }
        if (second < lead.second_min) {
            return std::unexpected(error_at(i, lead.second_reason));
        }
        if (second > lead.second_max) {
            return std::unexpected(error_at(i, lead.second_reason_high));
        }
        char32_t cp = (first & lead.payload_mask);
        cp = (cp << 6) | (second & 0x3FU);
        for (int k = 2; k < lead.length; ++k) {
            const std::size_t at = i + static_cast<std::size_t>(k);
            if (at >= text.size() || !is_continuation(static_cast<unsigned char>(text[at]))) {
                return std::unexpected(error_at(i, "truncated sequence"));
            }
            cp = (cp << 6) | (static_cast<unsigned char>(text[at]) & 0x3FU);
        }
        assert(cp <= kMaxCodePoint && !is_surrogate(cp));
        out.push_back(cp);
        i += static_cast<std::size_t>(lead.length);
    }
    return out;
}

void append(std::string &out, const char32_t cp) {
    assert(cp <= kMaxCodePoint && !is_surrogate(cp));
    const auto push = [&out](const char32_t value) { out.push_back(static_cast<char>(value)); };
    if (cp < 0x80) {
        push(cp);
    } else if (cp < 0x800) {
        push(0xC0 | (cp >> 6));
        push(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        push(0xE0 | (cp >> 12));
        push(0x80 | ((cp >> 6) & 0x3F));
        push(0x80 | (cp & 0x3F));
    } else {
        push(0xF0 | (cp >> 18));
        push(0x80 | ((cp >> 12) & 0x3F));
        push(0x80 | ((cp >> 6) & 0x3F));
        push(0x80 | (cp & 0x3F));
    }
}

std::string encode(const char32_t cp) {
    std::string out;
    append(out, cp);
    return out;
}

}  // namespace tellmeutf::utf8
