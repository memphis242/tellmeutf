#include "check.hpp"
#include "wordpiece.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace sem = tellmeutf::semantic;

namespace {

std::string read_file(const std::string &path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    return content.str();
}

std::string unescape(const std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\\' || i + 1 == text.size()) {
            out.push_back(text[i]);
            continue;
        }
        switch (text[++i]) {
        case 't': out.push_back('\t'); break;
        case 'n': out.push_back('\n'); break;
        case 'r': out.push_back('\r'); break;
        default: out.push_back(text[i]); break;
        }
    }
    return out;
}

std::vector<std::int64_t> parse_ids(const std::string_view text) {
    std::vector<std::int64_t> ids;
    std::istringstream in{std::string(text)};
    std::int64_t id = 0;
    while (in >> id) {
        ids.push_back(id);
    }
    return ids;
}

sem::WordPiece make(const std::size_t max_length = sem::WordPiece::kDefaultMaxLength) {
    const std::string vocab = read_file(TELLMEUTF_SOURCE_DIR "/data/semantic/vocab.txt");
    CHECK(!vocab.empty());
    auto tokenizer = sem::WordPiece::create(vocab, max_length);
    if (!tokenizer) {
        std::fprintf(stderr, "cannot create the tokenizer: %s\n", tokenizer.error().c_str());
        std::exit(1);
    }
    return std::move(*tokenizer);
}

void test_golden() {
    const sem::WordPiece tokenizer = make();
    CHECK(tokenizer.vocab_size() == 30522);
    const std::string golden = read_file(TELLMEUTF_SOURCE_DIR "/tests/data/wordpiece_golden.tsv");
    std::istringstream lines(golden);
    std::string line;
    int cases = 0;
    int mismatches = 0;
    while (std::getline(lines, line)) {
        const std::size_t tab = line.find('\t');
        CHECK(tab != std::string::npos);
        if (tab == std::string::npos) {
            continue;
        }
        const std::string text = unescape(std::string_view(line).substr(0, tab));
        const std::vector<std::int64_t> expected = parse_ids(std::string_view(line).substr(tab + 1));
        ++cases;
        if (tokenizer.encode(text) != expected) {
            std::fprintf(stderr, "golden mismatch for line: %s\n", line.c_str());
            ++mismatches;
        }
    }
    CHECK(cases >= 300);
    CHECK(mismatches == 0);
}

void test_limits_and_robustness() {
    const sem::WordPiece tokenizer = make();
    const std::vector<std::int64_t> empty = tokenizer.encode("");
    CHECK(empty.size() == 2);
    CHECK(tokenizer.encode("   \t ") == empty);

    const std::string long_text(2000, 'a');
    CHECK(tokenizer.encode(long_text).size() == 3);  // one over-long word is one [UNK]
    std::string many_words;
    for (int i = 0; i < 500; ++i) {
        many_words += "alpha beta ";
    }
    const std::vector<std::int64_t> truncated = tokenizer.encode(many_words);
    CHECK(truncated.size() == tokenizer.max_length());
    CHECK(truncated.front() == tokenizer.encode("x").front());
    CHECK(truncated.back() == tokenizer.encode("x").back());

    // Invalid UTF-8 is dropped rather than rejected.
    CHECK(tokenizer.encode("a\xff\xfe" "b") == tokenizer.encode("ab"));
    CHECK(tokenizer.encode("\xe2\x82") == empty);

    const sem::WordPiece short_one = make(5);
    CHECK(short_one.encode("greek small letter alpha").size() == 5);
}

void test_create_errors() {
    CHECK(!sem::WordPiece::create("[PAD]\n[UNK]\n[CLS]\nhello\n").has_value());  // no [SEP]
    CHECK(!sem::WordPiece::create("").has_value());
    CHECK(!sem::WordPiece::create("[PAD]\n[UNK]\n[CLS]\n[SEP]\n", 1).has_value());
    const auto tiny = sem::WordPiece::create("[PAD]\n[UNK]\n[CLS]\n[SEP]\nhi\n##s\n");
    CHECK(tiny.has_value());
    if (tiny) {
        CHECK((tiny->encode("HIS hi") == std::vector<std::int64_t>{2, 4, 5, 4, 3}));
        CHECK((tiny->encode("zzz") == std::vector<std::int64_t>{2, 1, 3}));
    }
}

}  // namespace

int main() {
    test_golden();
    test_limits_and_robustness();
    test_create_errors();
    return tellmeutf::test::finish();
}
