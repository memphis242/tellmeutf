#include "check.hpp"
#include "ucd.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace ucd = tellmeutf::ucd;

namespace {

// One development query: the code point(s) the user meant (official names,
// separated by ';'; any of them counts) and the worst rank that is acceptable.
struct Case {
    std::string_view query;
    std::string_view meant;
    std::size_t max_rank;
};

constexpr std::array kCases = {
    // Single-character typos: deletion, insertion, substitution, transposition.
    Case{"chek mark", "CHECK MARK", 1},
    Case{"check mrak", "CHECK MARK", 1},
    Case{"chek marc", "CHECK MARK", 1},
    Case{"snowmann", "SNOWMAN", 1},
    Case{"snowmna", "SNOWMAN", 1},
    Case{"snwman", "SNOWMAN", 1},
    Case{"hart", "BLACK HEART SUIT;HEAVY BLACK HEART;BLACK HEART;WHITE HEART SUIT", 5},
    Case{"degree celcius", "DEGREE CELSIUS", 1},
    Case{"copyrigt", "COPYRIGHT SIGN", 1},
    Case{"copyrite sign", "COPYRIGHT SIGN", 1},
    Case{"infinty", "INFINITY", 1},
    Case{"umbrela", "UMBRELLA", 1},
    Case{"rockett", "ROCKET", 1},
    Case{"bulet", "BULLET", 1},
    Case{"black starr", "BLACK STAR", 1},
    Case{"beer mugg", "BEER MUG", 1},
    Case{"crying fase", "CRYING FACE", 1},
    Case{"winking facce", "WINKING FACE", 1},
    Case{"squre root", "SQUARE ROOT", 1},
    Case{"not equl to", "NOT EQUAL TO", 1},
    Case{"registerd sign", "REGISTERED SIGN", 1},
    Case{"greek small leter alpha", "GREEK SMALL LETTER ALPHA", 1},
    Case{"zero widht joiner", "ZERO WIDTH JOINER", 3},
    Case{"summaton", "N-ARY SUMMATION", 3},
    // Word-boundary slips: run together, split apart, hyphen forms.
    Case{"checkmark", "CHECK MARK;HEAVY CHECK MARK;WHITE HEAVY CHECK MARK", 3},
    Case{"rightarrow", "RIGHTWARDS ARROW;BLACK RIGHTWARDS ARROW", 3},
    Case{"blackheart", "BLACK HEART SUIT;BLACK HEART", 2},
    Case{"black heartsuit", "BLACK HEART SUIT", 1},
    Case{"pile ofpoo", "PILE OF POO", 1},
    Case{"pileofpoo", "PILE OF POO", 1},
    Case{"partypopper", "PARTY POPPER", 1},
    Case{"thumbsup", "THUMBS UP SIGN", 1},
    Case{"tickk", "HEAVY CHECK MARK;WHITE HEAVY CHECK MARK;BALLOT BOX WITH CHECK", 3},  // typo of a CLDR keyword
    Case{"emdash", "EM DASH", 1},
    Case{"endash", "EN DASH", 1},
    Case{"zerowidth joiner", "ZERO WIDTH JOINER", 1},
    Case{"nobreak space", "NO-BREAK SPACE", 1},
    Case{"nonbreaking space", "NO-BREAK SPACE", 3},
    Case{"trademark", "TRADE MARK SIGN", 1},
    // Extra word, plural and stem variants.
    Case{"snowman symbol", "SNOWMAN", 1},
    Case{"snowman sign", "SNOWMAN", 1},
    Case{"check mark symbol", "CHECK MARK", 3},
    Case{"heavy check mark symbol", "HEAVY CHECK MARK", 1},
    // Several words, one wrong.
    Case{"pile of poop", "PILE OF POO", 1},
    Case{"smiling face with hart eyes", "SMILING FACE WITH HEART-SHAPED EYES", 1},
    Case{"rightwards arow", "RIGHTWARDS ARROW", 1},
    Case{"black heart sut", "BLACK HEART SUIT", 1},
    Case{"less than or equl to", "LESS-THAN OR EQUAL TO", 1},
    // Aliases with a typo.
    Case{"nbps", "NBSP", 1},
    Case{"nbsq", "NBSP", 1},
};
static_assert(kCases.size() >= 40);

bool is_meant(const char32_t cp, const std::string_view meant) {
    std::size_t begin = 0;
    while (begin <= meant.size()) {
        const std::size_t end = std::min(meant.find(';', begin), meant.size());
        const std::vector<char32_t> exact = ucd::find_exact(std::string(meant.substr(begin, end - begin)));
        if (std::ranges::find(exact, cp) != exact.end()) {
            return true;
        }
        begin = end + 1;
    }
    return false;
}

void test_development_set() {
    std::size_t first = 0;
    std::size_t third = 0;
    for (const struct Case &c : kCases) {
        const std::vector<ucd::FuzzyMatch> matches = ucd::fuzzy_search(c.query, 5);
        std::size_t rank = 0;
        for (std::size_t i = 0; i < matches.size() && rank == 0; ++i) {
            if (is_meant(matches[i].cp, c.meant)) {
                rank = i + 1;
            }
        }
        if (rank == 0 || rank > c.max_rank) {
            const std::string what = "query '" + std::string(c.query) + "' meant " + std::string(c.meant) +
                                     ": rank " + std::to_string(rank) + ", allowed " + std::to_string(c.max_rank);
            tellmeutf::test::report_failure(__FILE__, __LINE__, what);
        }
        first += rank == 1 ? 1 : 0;
        third += rank >= 1 && rank <= 3 ? 1 : 0;
    }
    // The set as a whole stays accurate even if one entry is later re-tuned.
    CHECK(first * 10 >= kCases.size() * 8);
    CHECK(third * 20 >= kCases.size() * 19);
}

void test_official_name_wins_a_tie() {
    // U+26C4 is also called "snowman" by CLDR, with the same word evidence as
    // the official SNOWMAN (U+2603); the official name leads by a clear margin.
    const std::vector<ucd::FuzzyMatch> matches = ucd::fuzzy_search("snowmann", 5);
    CHECK(matches.size() >= 2);
    CHECK(matches.size() >= 2 && matches[0].cp == 0x2603 && matches[1].cp == 0x26C4);
    CHECK(ucd::is_confident(matches));
}

void test_strict_answers_survive() {
    // A name the strict path resolves is also the best fuzzy match.
    for (const std::string_view name : {"HEAVY CHECK MARK", "SNOWMAN", "PILE OF POO", "LINE FEED", "NBSP", "ZERO WIDTH JOINER"}) {
        const std::vector<char32_t> exact = ucd::find_exact(std::string(name));
        CHECK(exact.size() == 1);
        const std::vector<ucd::FuzzyMatch> matches = ucd::fuzzy_search(name, 3);
        CHECK(!matches.empty());
        CHECK(!matches.empty() && !exact.empty() && matches.front().cp == exact.front());
    }
}

bool is_hangul_syllable(const char32_t cp) { return cp >= 0xAC00 && cp <= 0xD7A3; }

bool is_algorithmic_or_unnamed(const char32_t cp) {
    const std::string name = ucd::lookup(cp).name;
    return is_hangul_syllable(cp) || name.starts_with('<') || name.starts_with("CJK UNIFIED IDEOGRAPH-") ||
           name.starts_with("CJK COMPATIBILITY IDEOGRAPH-") || name.starts_with("TANGUT IDEOGRAPH-") ||
           name.starts_with("JURCHEN CHARACTER-") || name.starts_with("SEAL CHARACTER-");
}

void test_hangul_is_opt_in() {
    // A typo never resolves to an algorithmic Hangul syllable name.
    const std::vector<ucd::FuzzyMatch> allow = ucd::fuzzy_search("allow", 50);
    for (const ucd::FuzzyMatch &match : allow) {
        CHECK(!is_hangul_syllable(match.cp));
    }
    // It only hides inside a longer word of another name (SHALLOW PAN OF FOOD), which is not evidence to act on.
    CHECK(!ucd::is_confident(allow));
    // Mentioning HANGUL or SYLLABLE (even misspelled) makes them eligible.
    for (const std::string_view query : {"hangul syllable gagg", "hangul sylable gag", "hangl syllable gagg"}) {
        const std::vector<ucd::FuzzyMatch> matches = ucd::fuzzy_search(query, 5);
        CHECK(std::ranges::any_of(matches, [](const ucd::FuzzyMatch &m) { return is_hangul_syllable(m.cp); }));
    }
}

void test_common_words_never_confidently_unnamed() {
    // Everyday words a person might type for a symbol. Those that resolve
    // strictly never reach the fuzzy path and are skipped.
    constexpr std::array kWords = {
        "smiley", "coffee", "sunny", "laptop", "sunshine", "burger", "tickmark", "thumbsdown",
        "heartbreak", "poop", "dollars", "tm", "infinite", "rainy", "cloudy", "snowy",
        "happyface", "sadface", "hashtag", "mailboxes", "checkbox", "cellphone", "dice", "tick",
        "bolt", "newline", "thumb", "pizza", "rain", "ghost", "doggo", "chilly", "bunnies",
        "downarrow", "uparrow", "thumbup", "lightbulb", "tearsofjoy", "highfive", "peacesign",
        "raincloud", "bigsmile", "frowny", "gloomy", "sparkly", "nerdy", "warmth", "cooler",
        "lovely", "sadness", "joyful", "tearful", "grumpy", "sillyface", "partyface"};
    std::size_t probed = 0;
    for (const std::string_view word : kWords) {
        if (!ucd::find_exact(std::string(word)).empty() || !ucd::search(word).empty()) {
            continue;
        }
        ++probed;
        const std::vector<ucd::FuzzyMatch> matches = ucd::fuzzy_search(word, 3);
        std::string shown;
        for (const ucd::FuzzyMatch &m : matches) {
            shown += " [" + ucd::lookup(m.cp).name + "]";
        }
        std::printf("fuzzy probe '%.*s': %s%s\n", static_cast<int>(word.size()), word.data(),
                    ucd::is_confident(matches) ? "confident" : "list", shown.c_str());
        if (ucd::is_confident(matches)) {
            CHECK(!is_algorithmic_or_unnamed(matches.front().cp));
        }
        for (const ucd::FuzzyMatch &m : matches) {
            CHECK(!is_hangul_syllable(m.cp));
        }
    }
    CHECK(probed >= 15);
}

void test_weak_typos_are_not_confident() {
    // One short word that is two edits from another word proves nothing.
    CHECK(!ucd::is_confident(ucd::fuzzy_search("annual", 5)));
    // Two edits from an everyday word that only CLDR text uses (BACKPACK) is no evidence either.
    CHECK(!ucd::is_confident(ucd::fuzzy_search("backtick", 5)));
    // The same typos on long words, or glued names, are trustworthy.
    for (const std::string_view query : {"pinapple", "elephent", "strawbery", "hamburgr", "lightening", "chek mark",
                                         "snowmann", "thumbs upp", "fullstop", "questionmark", "exclamationmark",
                                         "pile of poo emoji"}) {
        CHECK(ucd::is_confident(ucd::fuzzy_search(query, 5)));
    }
}

void test_nonsense() {
    for (const std::string_view query : {"qzxjv", "xxxxxxxx", "kkkkkkkkkkkk", "zzzz qqqq"}) {
        const std::vector<ucd::FuzzyMatch> matches = ucd::fuzzy_search(query, 10);
        CHECK(!ucd::is_confident(matches));
    }
}

void test_edge_inputs() {
    CHECK(ucd::fuzzy_search("", 10).empty());
    CHECK(ucd::fuzzy_search("   \t_ ", 10).empty());
    CHECK(ucd::fuzzy_search("---", 10).empty());
    CHECK(ucd::fuzzy_search("snowman", 0).empty());
    CHECK(ucd::fuzzy_search("sn\xC3\xB6wman", 10).empty());  // non-ASCII bytes
    CHECK(ucd::fuzzy_search("\xFF\xFE", 10).empty());
    CHECK(ucd::fuzzy_search(std::string(100000, 'a'), 10).empty());
    CHECK(ucd::fuzzy_search(std::string("a b c d e f g h i j k l m n o p q r s t u v w x y z ") + "snowman", 10).size() <= 10);
    const std::string nul("snow\0man", 8);
    (void)ucd::fuzzy_search(nul, 5);  // must not crash
    CHECK(ucd::fuzzy_search("a", 3).size() <= 3);
    CHECK(ucd::fuzzy_search("12345", 3).size() <= 3);
    CHECK(ucd::fuzzy_search("snowman", 100000).size() <= 100000);
}

void test_result_shape() {
    const std::vector<ucd::FuzzyMatch> matches = ucd::fuzzy_search("arrow head", 50);
    CHECK(!matches.empty());
    CHECK(matches.size() <= 50);
    for (std::size_t i = 0; i < matches.size(); ++i) {
        CHECK(matches[i].score > 0.0 && matches[i].score <= 1.0);
        CHECK(matches[i].cp <= ucd::kLastCodePoint);
        if (i > 0) {
            CHECK(matches[i - 1].score >= matches[i].score);
        }
        for (std::size_t j = 0; j < i; ++j) {
            CHECK(matches[j].cp != matches[i].cp);
        }
    }
    // A smaller limit returns a prefix of the same ranking, and the ranking is repeatable.
    const std::vector<ucd::FuzzyMatch> few = ucd::fuzzy_search("arrow head", 5);
    CHECK(few.size() == std::min<std::size_t>(5, matches.size()));
    for (std::size_t i = 0; i < few.size(); ++i) {
        CHECK(few[i].cp == matches[i].cp);
    }
    const std::vector<ucd::FuzzyMatch> again = ucd::fuzzy_search("arrow head", 50);
    CHECK(again.size() == matches.size());
    for (std::size_t i = 0; i < again.size() && i < matches.size(); ++i) {
        CHECK(again[i].cp == matches[i].cp && again[i].score == matches[i].score);
    }
    // Case and separators do not matter.
    CHECK(ucd::fuzzy_search("CHEK_MARK", 1).front().cp == ucd::fuzzy_search("chek mark", 1).front().cp);
}

void test_confidence_rule() {
    const auto match = [](const double score, const double word, const double glue, const bool weak = false) {
        struct ucd::FuzzyMatch m;
        m.cp = 0x41;
        m.score = score;
        m.word_score = word;
        m.char_score = glue;
        m.char_coverage = glue;
        m.weak_word_match = weak;
        return m;
    };
    CHECK(!ucd::is_confident({}));
    CHECK(ucd::is_confident({match(0.95, 0.9, 0.0)}));
    CHECK(!ucd::is_confident({match(0.20, 0.9, 0.9)}));
    CHECK(ucd::is_confident({match(0.95, 0.9, 0.0), match(0.40, 0.4, 0.0)}));
    CHECK(!ucd::is_confident({match(0.95, 0.9, 0.0), match(0.94, 0.9, 0.0)}));
    CHECK(!ucd::is_confident({match(0.50, 0.9, 0.0), match(0.10, 0.1, 0.0)}));
    // Weak word evidence, or too little of it, does not carry a confident answer ...
    CHECK(!ucd::is_confident({match(0.95, 0.9, 0.0, true)}));
    CHECK(!ucd::is_confident({match(0.95, 0.2, 0.3)}));
    // ... unless the query is essentially the label with its spaces removed.
    CHECK(ucd::is_confident({match(0.95, 0.0, 0.95)}));
    CHECK(ucd::is_confident({match(0.95, 0.2, 0.9, true)}));
}

}  // namespace

int main() {
    test_development_set();
    test_strict_answers_survive();
    test_official_name_wins_a_tie();
    test_hangul_is_opt_in();
    test_common_words_never_confidently_unnamed();
    test_weak_typos_are_not_confident();
    test_nonsense();
    test_edge_inputs();
    test_result_shape();
    test_confidence_rule();
    return tellmeutf::test::finish();
}
