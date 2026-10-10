#include "wordpiece.hpp"

#include <algorithm>
#include <cassert>

namespace tellmeutf::semantic {

namespace {

[[nodiscard]] constexpr bool is_ascii_alnum(const char32_t c) {
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

// BERT treats every ASCII character that is not a letter, digit, space or
// control as punctuation.
[[nodiscard]] constexpr bool is_ascii_punctuation(const char32_t c) {
    return c > 0x20 && c < 0x7F && !is_ascii_alnum(c);
}

[[nodiscard]] constexpr bool is_whitespace(const char32_t c) {
    switch (c) {
    case U' ':
    case U'\t':
    case U'\n':
    case U'\r':
    case 0x00A0:
    case 0x1680:
    case 0x202F:
    case 0x205F:
    case 0x3000:
        return true;
    default:
        return c >= 0x2000 && c <= 0x200A;
    }
}

[[nodiscard]] constexpr bool is_removed(const char32_t c) {
    return c == 0 || c == 0xFFFD || (c < 0x20 && !is_whitespace(c)) || (c >= 0x7F && c <= 0x9F);
}

// Next code point of `text` at `pos` (advancing `pos`), or 0xFFFD for a byte
// that does not start a well-formed sequence (that byte alone is skipped).
[[nodiscard]] char32_t next_code_point(const std::string_view text, std::size_t &pos) {
    const auto byte = [&](const std::size_t i) { return static_cast<unsigned char>(text[i]); };
    const unsigned char lead = byte(pos);
    std::size_t length = 1;
    char32_t cp = lead;
    char32_t minimum = 0;
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
        cp = lead & 0x1FU;
        minimum = 0x80;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        cp = lead & 0x0FU;
        minimum = 0x800;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        cp = lead & 0x07U;
        minimum = 0x10000;
    } else if (lead >= 0x80) {
        ++pos;
        return 0xFFFD;
    }
    if (length > 1) {
        if (pos + length > text.size()) {
            ++pos;
            return 0xFFFD;
        }
        for (std::size_t i = 1; i < length; ++i) {
            const unsigned char cont = byte(pos + i);
            if ((cont & 0xC0U) != 0x80U) {
                ++pos;
                return 0xFFFD;
            }
            cp = (cp << 6) | (cont & 0x3FU);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
            ++pos;
            return 0xFFFD;
        }
    }
    pos += length;
    return cp;
}

}  // namespace

std::expected<WordPiece, std::string> WordPiece::create(const std::string_view vocab, const std::size_t max_length) {
    if (max_length < 2) {
        return std::unexpected("the maximum token count must be at least 2");
    }
    WordPiece result;
    result.max_length_ = max_length;
    std::int64_t id = 0;
    std::size_t pos = 0;
    while (pos < vocab.size()) {
        std::size_t end = vocab.find('\n', pos);
        if (end == std::string_view::npos) {
            end = vocab.size();
        }
        std::string_view token = vocab.substr(pos, end - pos);
        if (token.ends_with('\r')) {
            token.remove_suffix(1);
        }
        result.ids_.try_emplace(std::string(token), id);  // the first line of a repeated token wins
        ++id;
        pos = end + 1;
    }
    result.vocab_size_ = static_cast<std::size_t>(id);

    const auto special = [&](const std::string_view token) -> std::expected<std::int64_t, std::string> {
        const auto it = result.ids_.find(token);
        if (it == result.ids_.end()) {
            return std::unexpected("the vocabulary has no " + std::string(token) + " token");
        }
        return it->second;
    };
    const auto pad = special("[PAD]");
    const auto unk = special("[UNK]");
    const auto cls = special("[CLS]");
    const auto sep = special("[SEP]");
    for (const auto *const r : {&pad, &unk, &cls, &sep}) {
        if (!r->has_value()) {
            return std::unexpected(r->error());
        }
    }
    result.pad_ = *pad;
    result.unk_ = *unk;
    result.cls_ = *cls;
    result.sep_ = *sep;
    return result;
}

// `word` is a run of lowercase ASCII letters and digits, a single ASCII
// punctuation character, or the UTF-8 form of a single non-ASCII code point.
void WordPiece::append_word(const std::string_view word, std::vector<std::int64_t> &out) const {
    assert(!word.empty());
    const bool ascii = static_cast<unsigned char>(word.front()) < 0x80;
    if (!ascii) {
        const auto it = ids_.find(word);
        out.push_back(it == ids_.end() ? unk_ : it->second);
        return;
    }
    if (word.size() > kMaxWordChars) {
        out.push_back(unk_);
        return;
    }
    const std::size_t first_piece = out.size();
    std::string piece;
    std::size_t start = 0;
    while (start < word.size()) {
        std::size_t end = word.size();
        bool found = false;
        while (end > start) {
            piece.assign(start > 0 ? "##" : "");
            piece.append(word.substr(start, end - start));
            const auto it = ids_.find(piece);
            if (it != ids_.end()) {
                out.push_back(it->second);
                found = true;
                break;
            }
            --end;
        }
        if (!found) {
            out.resize(first_piece);
            out.push_back(unk_);
            return;
        }
        start = end;
    }
}

std::vector<std::int64_t> WordPiece::encode(const std::string_view text) const {
    std::vector<std::int64_t> ids;
    ids.push_back(cls_);
    std::string word;
    const auto flush = [&] {
        if (!word.empty()) {
            append_word(word, ids);
            word.clear();
        }
    };
    std::size_t pos = 0;
    while (pos < text.size()) {
        const char32_t cp = next_code_point(text, pos);
        if (is_removed(cp)) {
            continue;
        }
        if (is_whitespace(cp)) {
            flush();
        } else if (is_ascii_alnum(cp)) {
            const char lower = static_cast<char>(cp >= 'A' && cp <= 'Z' ? cp - 'A' + 'a' : cp);
            word.push_back(lower);
        } else if (is_ascii_punctuation(cp)) {
            flush();
            word.push_back(static_cast<char>(cp));
            flush();
        } else {
            flush();
            // UTF-8 form of a non-ASCII code point.
            if (cp < 0x800) {
                word.push_back(static_cast<char>(0xC0U | (cp >> 6)));
                word.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            } else if (cp < 0x10000) {
                word.push_back(static_cast<char>(0xE0U | (cp >> 12)));
                word.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
                word.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            } else {
                word.push_back(static_cast<char>(0xF0U | (cp >> 18)));
                word.push_back(static_cast<char>(0x80U | ((cp >> 12) & 0x3FU)));
                word.push_back(static_cast<char>(0x80U | ((cp >> 6) & 0x3FU)));
                word.push_back(static_cast<char>(0x80U | (cp & 0x3FU)));
            }
            flush();
        }
        if (ids.size() >= max_length_) {
            break;  // enough pieces; the rest would be truncated
        }
    }
    flush();
    if (ids.size() > max_length_ - 1) {
        ids.resize(max_length_ - 1);
    }
    ids.push_back(sep_);
    assert(ids.size() <= max_length_);
    return ids;
}

}  // namespace tellmeutf::semantic
