// BERT uncased WordPiece tokenizer, matching the model's tokenizer for ASCII
// text. No file access: the vocabulary is passed in as text.
#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tellmeutf::semantic {

class WordPiece {
public:
    static constexpr std::size_t kDefaultMaxLength = 64;
    // Words longer than this many characters become [UNK].
    static constexpr std::size_t kMaxWordChars = 100;

    // `vocab` holds one token per line; a token's id is its line index. Fails
    // (message in the error) when [PAD], [UNK], [CLS] or [SEP] are missing or
    // when `max_length` is below 2.
    [[nodiscard]] static std::expected<WordPiece, std::string> create(std::string_view vocab,
                                                                      std::size_t max_length = kDefaultMaxLength);

    // Ids of "[CLS] pieces... [SEP]", at most max_length() ids (pieces are
    // dropped from the end when the text is longer).
    //
    // Text handling: invalid UTF-8, NUL, U+FFFD and control characters are
    // removed; whitespace separates words; ASCII letters are lowercased; each
    // ASCII punctuation character and each non-ASCII code point is a word of
    // its own that is looked up in the vocabulary whole and otherwise becomes
    // [UNK]. Unlike the reference tokenizer, accents are not stripped, non-ASCII
    // letters are not lowercased, and non-ASCII punctuation does not split
    // words.
    [[nodiscard]] std::vector<std::int64_t> encode(std::string_view text) const;

    [[nodiscard]] std::size_t max_length() const { return max_length_; }
    [[nodiscard]] std::size_t vocab_size() const { return vocab_size_; }
    [[nodiscard]] std::int64_t pad_id() const { return pad_; }

private:
    struct StringHash {
        using is_transparent = void;
        [[nodiscard]] std::size_t operator()(const std::string_view s) const { return std::hash<std::string_view>{}(s); }
    };

    WordPiece() = default;

    void append_word(std::string_view word, std::vector<std::int64_t> &out) const;

    std::unordered_map<std::string, std::int64_t, StringHash, std::equal_to<>> ids_;
    std::size_t vocab_size_ = 0;
    std::size_t max_length_ = kDefaultMaxLength;
    std::int64_t pad_ = 0;
    std::int64_t unk_ = 0;
    std::int64_t cls_ = 0;
    std::int64_t sep_ = 0;
};

}  // namespace tellmeutf::semantic
