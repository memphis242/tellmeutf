// Approximate name retrieval over every name and alias (see docs/design.md,
// "Approximate fallback").
//
// Each label is scored against the query by two signals that are fused:
//   - a word signal: an idf-weighted F-measure of the label's words against the
//     query's words, where words count as equal when they are close (same
//     light stem, prefix, or a few edits apart);
//   - a character signal: Dice similarity of the character trigrams of the
//     query and the label with the spaces removed, which survives words that
//     are run together or split apart.
// The statistics and inverted indexes behind both are built on the first call.
#include "ucd.hpp"

#include "ucd_internal.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <span>
#include <utility>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace tellmeutf::ucd {

namespace {

// ---------------------------------------------------------------------------
// Tuning constants

// Queries longer than this (in bytes) are not searched; labels are far shorter.
constexpr std::size_t kMaxQueryBytes = 200;
// Words of the query beyond this many are ignored.
constexpr std::size_t kMaxQueryWords = 12;
// Words longer than this take part only by equality, stem and prefix.
constexpr std::size_t kMaxEditWordLength = 40;

// A vocabulary word counts as similar to a query word at or above this value.
constexpr double kMinWordSimilarity = 0.5;
// Similarity of two different words that share a light stem.
constexpr double kStemSimilarity = 0.9;
// Similarity of a query word that is a proper prefix of a label word: a base
// plus a share that grows with the fraction of the label word typed.
constexpr double kPrefixBase = 0.5;
constexpr double kPrefixSpan = 0.4;
// Shortest query word that may match as a prefix.
constexpr std::size_t kMinPrefixLength = 3;

// Edit-distance matches are scaled by a prior that favors frequent words, so a
// typo resolves to the common word it most likely was (HART to HEART, not HARP).
// The prior ranges from kTypoPriorFloor (a word in a single label) to 1 (a word
// in at least kFrequentWordLabels labels).
constexpr double kTypoPriorFloor = 0.75;
constexpr double kFrequentWordLabels = 100.0;
// A query word with no similar vocabulary word still counts toward the weight
// the label must cover, as a word that occurs in this many labels.
constexpr double kUnknownWordLabels = 2.0;

// Weight of recall against precision in the word signal's F-measure (beta
// squared). Above 1 favors covering the query over explaining the whole label,
// because a query is usually a fragment of the name.
constexpr double kRecallWeight = 2.0;

// Dice similarity below this is treated as no character evidence.
constexpr double kMinTrigramDice = 0.4;
// Scale of the character signal in the fusion; below 1 so that character
// evidence alone never outranks an equally strong word match.
constexpr double kTrigramScale = 0.95;

// A word match is strong (trustworthy enough to back a confident answer) when
// the words are equal, share a stem, the query word is a proper prefix of at
// least this many letters covering at least this share of the label word, or
// they differ by one edit and the longer word has at least kStrongOneEditLength
// letters, or by two edits and the longer word has at least kStrongTwoEditLength
// letters. A weak match on a shorter word is forgiven when another query word
// matches strongly in the same label, or when the query has several words and
// all of them match (the rest of the query vouches for it).
constexpr std::size_t kStrongPrefixLength = 4;
constexpr double kStrongPrefixShare = 0.6;
constexpr std::size_t kStrongOneEditLength = 6;
constexpr std::size_t kStrongTwoEditLength = 8;

// Labels whose fused score is below this are not reported.
constexpr double kMinScore = 0.4;

// is_confident: the best score must reach this and lead the next by the margin.
// And one kind of evidence must carry it alone: either the word score reaches
// kConfidentWordScore with every matched query word a strong match, or the
// character (glued trigram) similarity reaches kConfidentGlueScore, meaning the
// query is essentially the label with its spaces removed.
constexpr double kConfidentWordScore = 0.5;
// Glue evidence: Dice at least kConfidentGlueDice and at least this share of the
// query's trigrams found in the label (the label may carry extra words), for a
// query of at least kMinGlueTrigrams trigrams (short queries are too easy to
// find inside a longer glued label).
constexpr double kConfidentGlueDice = 0.6;
constexpr double kConfidentGlueCoverage = 0.85;
constexpr std::size_t kMinGlueTrigrams = 6;
constexpr double kConfidentScore = 0.65;
constexpr double kConfidentMargin = 0.04;
// A CLDR short name or keyword scores this share of what the same evidence
// earns an official name or alias, so an official name wins a tie. The gap
// at a perfect score is a little over kConfidentMargin.
constexpr double kAnnotationFactor = 0.95;

// ---------------------------------------------------------------------------
// Small string helpers

[[nodiscard]] constexpr bool is_alnum(const char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

[[nodiscard]] bool has_digit(const std::string_view word) {
    return std::ranges::any_of(word, [](const char c) { return c >= '0' && c <= '9'; });
}

[[nodiscard]] bool ends_with_at_least(const std::string_view word, const std::string_view suffix,
                                      const std::size_t min_stem) {
    return word.size() >= suffix.size() + min_stem && word.ends_with(suffix);
}

// A deliberately small stemmer: removes one of -ING, -ED, -LY, -ES, -S (never
// leaving fewer than three letters, never from -SS) and then a trailing E, so
// SMILE and SMILING, ARROWS and ARROW, BOXES and BOX share a stem.
[[nodiscard]] std::string light_stem(const std::string_view word) {
    constexpr std::size_t kMinStem = 3;
    std::string_view stem = word;
    if (has_digit(word)) {
        return std::string(word);
    }
    if (ends_with_at_least(stem, "ING", kMinStem)) {
        stem.remove_suffix(3);
    } else if (ends_with_at_least(stem, "ED", kMinStem)) {
        stem.remove_suffix(2);
    } else if (ends_with_at_least(stem, "LY", kMinStem)) {
        stem.remove_suffix(2);
    } else if (ends_with_at_least(stem, "ES", kMinStem)) {
        stem.remove_suffix(2);
    } else if (ends_with_at_least(stem, "S", kMinStem) && !stem.ends_with("SS")) {
        stem.remove_suffix(1);
    }
    if (stem.size() > kMinStem && stem.ends_with('E')) {
        stem.remove_suffix(1);
    }
    return std::string(stem);
}

// Optimal string alignment distance (substitution, insertion, deletion and
// adjacent transposition cost 1). Returns limit + 1 when it exceeds `limit`.
// Both strings must be at most kMaxEditWordLength long.
[[nodiscard]] std::size_t bounded_distance(const std::string_view a, const std::string_view b,
                                           const std::size_t limit) {
    assert(a.size() <= kMaxEditWordLength && b.size() <= kMaxEditWordLength);
    const std::size_t length_gap = a.size() > b.size() ? a.size() - b.size() : b.size() - a.size();
    if (length_gap > limit) {
        return limit + 1;
    }
    constexpr std::size_t kWidth = kMaxEditWordLength + 1;
    std::array<std::size_t, kWidth> older{};
    std::array<std::size_t, kWidth> previous{};
    std::array<std::size_t, kWidth> current{};
    for (std::size_t j = 0; j <= b.size(); ++j) {
        previous[j] = j;
    }
    for (std::size_t i = 1; i <= a.size(); ++i) {
        current[0] = i;
        std::size_t row_min = current[0];
        for (std::size_t j = 1; j <= b.size(); ++j) {
            const std::size_t cost = a[i - 1] == b[j - 1] ? 0 : 1;
            std::size_t best = std::min({previous[j] + 1, current[j - 1] + 1, previous[j - 1] + cost});
            if (i > 1 && j > 1 && a[i - 1] == b[j - 2] && a[i - 2] == b[j - 1]) {
                best = std::min(best, older[j - 2] + 1);
            }
            current[j] = best;
            row_min = std::min(row_min, best);
        }
        if (row_min > limit) {
            return limit + 1;
        }
        older = previous;
        previous = current;
    }
    return std::min(previous[b.size()], limit + 1);
}

// Edits tolerated in a query word of this length: none for 1 to 2 letters, one
// for 3 to 5, two from 6.
[[nodiscard]] std::size_t edit_allowance(const std::size_t length) {
    switch (length) {
    case 0:
    case 1:
    case 2:
        return 0;
    case 3:
    case 4:
    case 5:
        return 1;
    default:
        return 2;
    }
}

struct WordMatch {
    double similarity = 0.0;
    bool strong = false;
};

// Similarity in [0, 1] of a query word to a vocabulary word, ignoring stems
// (those are handled through the stem index). 0 when not similar enough.
// `prior` in (0, 1] scales the similarity of edit-distance matches. A match
// with two edits is only strong when `two_edits_can_be_strong`: an everyday
// word has many neighbors two edits away, so that is not evidence unless the
// word is a Unicode name word (see Index::official_word).
[[nodiscard]] struct WordMatch word_similarity(const std::string_view query, const std::string_view word, const double prior,
                                               const bool two_edits_can_be_strong = true) {
    if (query == word) {
        return {1.0, true};
    }
    if (has_digit(query) || has_digit(word)) {
        return {};
    }
    struct WordMatch best;
    if (query.size() >= kMinPrefixLength && word.size() > query.size() && word.starts_with(query)) {
        const double share = static_cast<double>(query.size()) / static_cast<double>(word.size());
        best.similarity = kPrefixBase + kPrefixSpan * share;
        best.strong = query.size() >= kStrongPrefixLength && share >= kStrongPrefixShare;
    }
    const std::size_t allowance = edit_allowance(query.size());
    if (allowance > 0 && query.size() <= kMaxEditWordLength && word.size() <= kMaxEditWordLength) {
        const std::size_t distance = bounded_distance(query, word, allowance);
        if (distance <= allowance) {
            const std::size_t longer_size = std::max(query.size(), word.size());
            const double similarity = prior * (1.0 - static_cast<double>(distance) / static_cast<double>(longer_size));
            const bool strong = distance == 1 ? longer_size >= kStrongOneEditLength
                                              : two_edits_can_be_strong && longer_size >= kStrongTwoEditLength;
            if (similarity > best.similarity) {
                best.similarity = similarity;
                best.strong = strong;
            }
        }
    }
    return best;
}

// ---------------------------------------------------------------------------
// Character trigrams of the label or query with everything but letters and
// digits removed, padded with one boundary mark on each side.

constexpr std::uint32_t kTrigramClasses = 37;
constexpr std::uint32_t kTrigramKeys = kTrigramClasses * kTrigramClasses * kTrigramClasses;

[[nodiscard]] std::uint32_t trigram_class(const char c) {
    if (c >= 'A' && c <= 'Z') {
        return static_cast<std::uint32_t>(c - 'A');
    }
    assert(c >= '0' && c <= '9');
    return 26 + static_cast<std::uint32_t>(c - '0');
}

constexpr std::uint32_t kPadClass = 36;

// Sorted distinct trigram keys of `text`; `text` is uppercase and may hold any
// bytes (those other than letters and digits are dropped).
[[nodiscard]] std::vector<std::uint32_t> trigram_keys(const std::string_view text) {
    std::vector<std::uint32_t> classes;
    classes.reserve(text.size() + 2);
    classes.push_back(kPadClass);
    for (const char c : text) {
        if (is_alnum(c)) {
            classes.push_back(trigram_class(c));
        }
    }
    classes.push_back(kPadClass);
    std::vector<std::uint32_t> keys;
    if (classes.size() == 2) {
        return keys;
    }
    keys.reserve(classes.size());
    for (std::size_t i = 0; i + 2 < classes.size(); ++i) {
        keys.push_back((classes[i] * kTrigramClasses + classes[i + 1]) * kTrigramClasses + classes[i + 2]);
    }
    std::ranges::sort(keys);
    const auto duplicates = std::ranges::unique(keys);
    keys.erase(duplicates.begin(), duplicates.end());
    return keys;
}

// ---------------------------------------------------------------------------
// The index

struct LabelRecord {
    char32_t cp = 0;
    internal::LabelOrigin origin = internal::LabelOrigin::Official;
    std::uint32_t first_word = 0;     // offset of the label's distinct word ids in Index::label_words
    std::uint32_t distinct_words = 0;
    std::uint32_t total_words = 0;    // including repeats, for tie-breaking
    std::uint32_t trigram_count = 0;  // distinct trigrams
    double idf_total = 0.0;           // summed idf of the distinct words
};

// What a label's evidence is multiplied by (see kAnnotationFactor).
[[nodiscard]] double origin_factor(const struct LabelRecord &record) {
    return record.origin == internal::LabelOrigin::Annotation ? kAnnotationFactor : 1.0;
}

// Offsets into a flat list: the entries of key k are list[offsets[k] .. offsets[k+1]).
struct Postings {
    std::vector<std::uint32_t> offsets;
    std::vector<std::uint32_t> list;

    [[nodiscard]] std::span<const std::uint32_t> of(const std::size_t key) const {
        return std::span<const std::uint32_t>(list).subspan(offsets[key], offsets[key + 1] - offsets[key]);
    }
};

// Builds Postings for `key_count` keys from (key, label) pairs given in label
// order.
[[nodiscard]] Postings build_postings(const std::size_t key_count,
                                      const std::vector<std::pair<std::uint32_t, std::uint32_t>> &pairs) {
    Postings postings;
    postings.offsets.assign(key_count + 1, 0);
    for (const auto &pair : pairs) {
        ++postings.offsets[pair.first + 1];
    }
    for (std::size_t k = 0; k < key_count; ++k) {
        postings.offsets[k + 1] += postings.offsets[k];
    }
    postings.list.resize(pairs.size());
    std::vector<std::uint32_t> fill(postings.offsets.begin(), postings.offsets.end() - 1);
    for (const auto &pair : pairs) {
        postings.list[fill[pair.first]++] = pair.second;
    }
    return postings;
}

struct Index {
    std::deque<std::string> words;  // vocabulary, stable addresses
    std::vector<double> idf;        // per vocabulary word
    std::vector<double> typo_prior; // per vocabulary word, see kTypoPriorFloor
    std::vector<bool> official_word; // per vocabulary word: in some name or alias, not only in CLDR text
    double unknown_word_idf = 0.0;
    std::unordered_map<std::string, std::vector<std::uint32_t>> by_stem;
    std::vector<LabelRecord> labels;
    std::vector<std::uint32_t> label_words;
    Postings word_labels;     // vocabulary word -> labels containing it
    Postings trigram_labels;  // trigram key -> labels containing it

    explicit Index(const bool include_hangul) {
        std::unordered_map<std::string_view, std::uint32_t> ids;
        std::vector<std::uint32_t> df;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> word_pairs;
        std::vector<std::pair<std::uint32_t, std::uint32_t>> trigram_pairs;
        std::vector<std::string_view> split;
        std::vector<std::uint32_t> distinct;
        // Capacities near the size of the data avoid repeated regrowth.
        constexpr std::size_t kExpectedLabels = 70000;
        constexpr std::size_t kExpectedWords = 30000;
        labels.reserve(kExpectedLabels);
        label_words.reserve(kExpectedLabels * 4);
        word_pairs.reserve(kExpectedLabels * 4);
        trigram_pairs.reserve(kExpectedLabels * 30);
        ids.reserve(kExpectedWords);

        internal::visit_labels(include_hangul, [&](const char32_t cp, const std::string_view label, const internal::LabelOrigin origin) {
            internal::split_words(label, split);
            distinct.clear();
            for (const std::string_view word : split) {
                const auto found = ids.find(word);
                if (found != ids.end()) {
                    distinct.push_back(found->second);
                } else {
                    const auto id = static_cast<std::uint32_t>(words.size());
                    words.emplace_back(word);
                    ids.emplace(std::string_view(words.back()), id);
                    df.push_back(0);
                    official_word.push_back(false);
                    distinct.push_back(id);
                }
                if (origin == internal::LabelOrigin::Official) {
                    official_word[distinct.back()] = true;
                }
            }
            if (split.empty()) {
                return;
            }
            const auto label_id = static_cast<std::uint32_t>(labels.size());
            struct LabelRecord record;
            record.cp = cp;
            record.origin = origin;
            record.total_words = static_cast<std::uint32_t>(distinct.size());
            std::ranges::sort(distinct);
            const auto duplicates = std::ranges::unique(distinct);
            distinct.erase(duplicates.begin(), duplicates.end());
            record.first_word = static_cast<std::uint32_t>(label_words.size());
            record.distinct_words = static_cast<std::uint32_t>(distinct.size());
            for (const std::uint32_t id : distinct) {
                label_words.push_back(id);
                ++df[id];
                word_pairs.emplace_back(id, label_id);
            }
            const std::vector<std::uint32_t> keys = trigram_keys(label);
            record.trigram_count = static_cast<std::uint32_t>(keys.size());
            for (const std::uint32_t key : keys) {
                trigram_pairs.emplace_back(key, label_id);
            }
            labels.push_back(record);
        });

        // BM25-style idf: rare words weigh more than common ones.
        const auto label_count = static_cast<double>(labels.size());
        idf.resize(words.size());
        typo_prior.resize(words.size());
        unknown_word_idf = std::log(1.0 + (label_count - kUnknownWordLabels + 0.5) / (kUnknownWordLabels + 0.5));
        for (std::size_t id = 0; id < words.size(); ++id) {
            const double frequency = static_cast<double>(df[id]);
            idf[id] = std::log(1.0 + (label_count - frequency + 0.5) / (frequency + 0.5));
            typo_prior[id] = kTypoPriorFloor + (1.0 - kTypoPriorFloor) * std::min(1.0, std::log1p(frequency) / std::log1p(kFrequentWordLabels));
            by_stem[light_stem(words[id])].push_back(static_cast<std::uint32_t>(id));
        }
        for (struct LabelRecord &record : labels) {
            for (std::uint32_t i = 0; i < record.distinct_words; ++i) {
                record.idf_total += idf[label_words[record.first_word + i]];
            }
        }
        word_labels = build_postings(words.size(), word_pairs);
        trigram_labels = build_postings(kTrigramKeys, trigram_pairs);
    }
};

// Hangul syllable names are algorithmic and meaningless to approximate
// matching, so the default index leaves them out (a typo must never resolve to
// one). The second index, built only on demand, includes them.
[[nodiscard]] const Index &index(const bool include_hangul) {
    static const Index without(false);
    if (!include_hangul) {
        return without;
    }
    static const Index with(true);
    return with;
}

// True when the query mentions Hangul syllables, that is, has a word similar
// to HANGUL or SYLLABLE under the usual word-similarity rules.
[[nodiscard]] bool asks_for_hangul(const std::vector<std::string_view> &query_words) {
    return std::ranges::any_of(query_words, [](const std::string_view word) {
        for (const std::string_view target : {std::string_view("HANGUL"), std::string_view("SYLLABLE")}) {
            if (word_similarity(word, target, 1.0).similarity >= kMinWordSimilarity || light_stem(word) == light_stem(target)) {
                return true;
            }
        }
        return false;
    });
}

// A vocabulary word a query word may stand for.
struct Similar {
    std::uint32_t word = 0;
    std::uint32_t query_word = 0;
    // Similarity times the word's idf, the idf capped at the query word's own
    // weight so that a rare near-miss never counts for more than the word typed.
    double credit = 0.0;
    bool strong = false;
};

// Per-label accumulator of the word signal.
struct WordEvidence {
    std::array<double, kMaxQueryWords> credit{};  // best similarity * idf per query word
    double explained = 0.0;                       // idf of label words explained by the query
    std::array<bool, kMaxQueryWords> strong{};    // query word has a strong match in the label
};

struct Scored {
    char32_t cp = 0;
    double score = 0.0;
    std::uint32_t words = 0;
    double word_score = 0.0;
    double char_score = 0.0;
    double char_coverage = 0.0;
    bool weak_word_match = false;
};

}  // namespace

std::vector<FuzzyMatch> fuzzy_search(const std::string_view query, const std::size_t limit) {
    if (limit == 0 || query.size() > kMaxQueryBytes) {
        return {};
    }
    if (std::ranges::any_of(query, [](const char c) { return static_cast<unsigned char>(c) >= 0x80; })) {
        return {};
    }
    const std::string upper = internal::normalize_query(query);
    std::vector<std::string_view> query_words;
    internal::split_words(upper, query_words);
    if (query_words.size() > kMaxQueryWords) {
        query_words.resize(kMaxQueryWords);
    }
    // Words made only of punctuation cannot match anything.
    std::erase_if(query_words, [](const std::string_view word) { return !std::ranges::any_of(word, is_alnum); });
    if (query_words.empty()) {
        return {};
    }

    const Index &idx = index(asks_for_hangul(query_words));
    const std::size_t label_count = idx.labels.size();

    // Character signal: shared trigram counts per label.
    const std::vector<std::uint32_t> query_trigrams = trigram_keys(upper);
    std::vector<std::uint16_t> shared(label_count, 0);
    std::vector<std::uint32_t> trigram_touched;
    for (const std::uint32_t key : query_trigrams) {
        for (const std::uint32_t label : idx.trigram_labels.of(key)) {
            if (shared[label]++ == 0) {
                trigram_touched.push_back(label);
            }
        }
    }
    const auto coverage = [&](const std::uint32_t label) {
        return query_trigrams.size() >= kMinGlueTrigrams
                   ? static_cast<double>(shared[label]) / static_cast<double>(query_trigrams.size())
                   : 0.0;
    };
    const auto dice = [&](const std::uint32_t label) {
        const double both = static_cast<double>(shared[label]);
        const double total = static_cast<double>(query_trigrams.size() + idx.labels[label].trigram_count);
        const double value = 2.0 * both / total;
        return value >= kMinTrigramDice ? value : 0.0;
    };

    // Word signal: which vocabulary words each query word may stand for.
    std::vector<Similar> similar;
    double query_weight = 0.0;
    for (std::size_t q = 0; q < query_words.size(); ++q) {
        const std::string_view word = query_words[q];
        std::unordered_map<std::uint32_t, struct WordMatch> found;
        for (std::size_t v = 0; v < idx.words.size(); ++v) {
            const struct WordMatch match = word_similarity(word, idx.words[v], idx.typo_prior[v], idx.official_word[v]);
            if (match.similarity >= kMinWordSimilarity) {
                found[static_cast<std::uint32_t>(v)] = match;
            }
        }
        if (const auto stem = idx.by_stem.find(light_stem(word)); stem != idx.by_stem.end()) {
            for (const std::uint32_t v : stem->second) {
                struct WordMatch &match = found[v];
                match.similarity = std::max(match.similarity, kStemSimilarity);
                match.strong = true;
            }
        }
        // The query word's weight is the idf of the word it most likely stands for.
        std::uint32_t likely = 0;
        double likely_similarity = 0.0;
        for (const auto &[v, match] : found) {
            const double similarity = match.similarity;
            if (similarity > likely_similarity || (similarity == likely_similarity && v < likely)) {
                likely = v;
                likely_similarity = similarity;
            }
        }
        const double weight = found.empty() ? idx.unknown_word_idf : idx.idf[likely];
        query_weight += weight;
        for (const auto &[v, match] : found) {
            similar.push_back(Similar{.word = v,
                                      .query_word = static_cast<std::uint32_t>(q),
                                      .credit = match.similarity * std::min(idx.idf[v], weight),
                                      .strong = match.strong});
        }
    }
    // Group by vocabulary word, in a fixed order, so scoring is deterministic.
    std::ranges::sort(similar, [](const struct Similar &a, const struct Similar &b) {
        return a.word != b.word ? a.word < b.word : a.query_word < b.query_word;
    });

    std::vector<std::int32_t> slot(label_count, -1);
    std::vector<std::uint32_t> word_touched;
    std::vector<struct WordEvidence> evidence;
    for (std::size_t begin = 0; begin < similar.size();) {
        std::size_t end = begin;
        double strongest = 0.0;
        while (end < similar.size() && similar[end].word == similar[begin].word) {
            strongest = std::max(strongest, similar[end].credit);
            ++end;
        }
        const double explained = strongest;
        for (const std::uint32_t label : idx.word_labels.of(similar[begin].word)) {
            if (slot[label] < 0) {
                slot[label] = static_cast<std::int32_t>(evidence.size());
                evidence.emplace_back();
                word_touched.push_back(label);
            }
            struct WordEvidence &entry = evidence[static_cast<std::size_t>(slot[label])];
            entry.explained += explained;
            for (std::size_t i = begin; i < end; ++i) {
                double &credit = entry.credit[similar[i].query_word];
                credit = std::max(credit, similar[i].credit);
                entry.strong[similar[i].query_word] = entry.strong[similar[i].query_word] || similar[i].strong;
            }
        }
        begin = end;
    }

    // Fusion. The word signal is the F-measure of recall (query weight covered)
    // and precision (label weight explained). The character signal is Dice
    // similarity. They are independent evidence for the same label, so they
    // combine as a noisy-or: either alone can carry a label, and agreement
    // between them lifts it above what either gives alone.
    const auto fuse = [](const double word_score, const double trigram_score) {
        return 1.0 - (1.0 - word_score) * (1.0 - kTrigramScale * trigram_score);
    };
    std::vector<struct Scored> scored;
    for (const std::uint32_t label : word_touched) {
        const struct LabelRecord &record = idx.labels[label];
        const struct WordEvidence &entry = evidence[static_cast<std::size_t>(slot[label])];
        double covered = 0.0;
        for (std::size_t q = 0; q < query_words.size(); ++q) {
            covered += entry.credit[q];
        }
        const double recall = query_weight > 0.0 ? std::min(1.0, covered / query_weight) : 0.0;
        const double precision = record.idf_total > 0.0 ? std::min(1.0, entry.explained / record.idf_total) : 0.0;
        const double denominator = kRecallWeight * precision + recall;
        const double word_score =
            denominator > 0.0 ? (1.0 + kRecallWeight) * precision * recall / denominator : 0.0;
        const double char_score = shared[label] != 0 ? dice(label) : 0.0;
        const double score = fuse(word_score, char_score) * origin_factor(record);
        bool any_strong = false;
        bool any_weak = false;
        bool all_matched = true;
        for (std::size_t q = 0; q < query_words.size(); ++q) {
            any_strong = any_strong || entry.strong[q];
            any_weak = any_weak || (entry.credit[q] > 0.0 && !entry.strong[q]);
            all_matched = all_matched && entry.credit[q] > 0.0;
        }
        // Several words that all match the label vouch for one another.
        const bool corroborated = any_strong || (query_words.size() >= 2 && all_matched);
        const bool weak = any_weak && !corroborated;
        if (score >= kMinScore) {
            scored.push_back(Scored{.cp = record.cp, .score = score, .words = record.total_words,
                                    .word_score = word_score, .char_score = char_score,
                                    .char_coverage = coverage(label), .weak_word_match = weak});
        }
    }
    for (const std::uint32_t label : trigram_touched) {
        if (slot[label] >= 0) {
            continue;
        }
        const double char_score = dice(label);
        const double score = fuse(0.0, char_score) * origin_factor(idx.labels[label]);
        if (score >= kMinScore) {
            scored.push_back(Scored{.cp = idx.labels[label].cp, .score = score, .words = idx.labels[label].total_words,
                                    .char_score = char_score, .char_coverage = coverage(label)});
        }
    }

    // One entry per code point (its best label), then best first.
    const auto better = [](const struct Scored &a, const struct Scored &b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.words != b.words) {
            return a.words < b.words;
        }
        return a.cp < b.cp;
    };
    std::ranges::sort(scored, [&](const struct Scored &a, const struct Scored &b) {
        return a.cp != b.cp ? a.cp < b.cp : better(a, b);
    });
    const auto duplicates = std::ranges::unique(scored, [](const struct Scored &a, const struct Scored &b) { return a.cp == b.cp; });
    scored.erase(duplicates.begin(), duplicates.end());
    const std::size_t kept = std::min(limit, scored.size());
    std::ranges::partial_sort(scored, scored.begin() + static_cast<std::ptrdiff_t>(kept), better);

    std::vector<FuzzyMatch> result;
    result.reserve(kept);
    for (std::size_t i = 0; i < kept; ++i) {
        assert(scored[i].score > 0.0 && scored[i].score <= 1.0 + 1e-9);
        result.push_back(FuzzyMatch{.cp = scored[i].cp,
                                    .score = std::min(1.0, scored[i].score),
                                    .word_score = scored[i].word_score,
                                    .char_score = scored[i].char_score,
                                    .char_coverage = scored[i].char_coverage,
                                    .weak_word_match = scored[i].weak_word_match});
    }
    return result;
}

bool is_confident(const std::vector<FuzzyMatch> &matches) {
    if (matches.empty() || matches.front().score < kConfidentScore) {
        return false;
    }
    if (matches.size() > 1 && matches[0].score - matches[1].score < kConfidentMargin) {
        return false;
    }
    const struct FuzzyMatch &best = matches.front();
    const bool word_evidence = best.word_score >= kConfidentWordScore && !best.weak_word_match;
    const bool glue_evidence = best.char_score >= kConfidentGlueDice && best.char_coverage >= kConfidentGlueCoverage;
    return word_evidence || glue_evidence;
}

}  // namespace tellmeutf::ucd
