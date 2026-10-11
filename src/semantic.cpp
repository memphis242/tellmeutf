// Facade for builds with -DTELLMEUTF_SEMANTIC=ON: finds the model files, loads
// or builds the index, embeds the query, and ranks.
#include "semantic.hpp"

#include "semantic_embedder.hpp"
#include "semantic_index.hpp"
#include "semantic_paths.hpp"
#include "ucd.hpp"
#include "wordpiece.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <sstream>

namespace tellmeutf::semantic {

namespace {

constexpr int kQueryThreads = 2;
constexpr int kBuildThreads = 8;
constexpr std::size_t kBuildBatch = 128;

[[nodiscard]] Error missing_file(const std::filesystem::path &file) {
    return Error{.message = "semantic search needs " + file.string() +
                            ", which is missing; run scripts/fetch-semantic.sh from the tellmeutf source "
                            "directory to download it (set TELLMEUTF_SEMANTIC_DIR to use another directory)"};
}

[[nodiscard]] std::expected<std::string, Error> read_whole_file(const std::filesystem::path &file) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(file, ec)) {
        return std::unexpected(missing_file(file));
    }
    std::ifstream in(file, std::ios::binary);
    std::ostringstream content;
    content << in.rdbuf();
    if (!in && !in.eof()) {
        return std::unexpected(Error{.message = "cannot read " + file.string()});
    }
    return content.str();
}

void tell(const std::function<void(std::string_view)> &notify, const std::string &message) {
    if (notify) {
        notify(message);
    }
}

struct Corpus {
    std::vector<char32_t> code_points;
    std::vector<std::string> texts;
    std::vector<float> weights;  // per row, see label_weight()
};

// What a row's similarity is multiplied by when ranking. Official names and
// aliases count in full. A CLDR short name is nearly as good, which only
// settles ties in favor of the official name. A CLDR keyword is a looser
// synonym shared by many characters: a one-word query equals the keyword row
// of every character that lists it, so unweighted those rows would crowd out
// the characters the word names. At this weight a keyword leads only when
// nothing else is close. The values come from measurements on a set of
// held-out descriptions (docs/design.md, "Semantic search").
constexpr float kShortNameWeight = 0.99f;
constexpr float kKeywordWeight = 0.7f;

[[nodiscard]] float label_weight(const ucd::LabelKind kind) {
    switch (kind) {
    case ucd::LabelKind::Name:
    case ucd::LabelKind::Alias:
        return 1.0f;
    case ucd::LabelKind::ShortName:
        return kShortNameWeight;
    case ucd::LabelKind::Keyword:
        return kKeywordWeight;
    }
    return 1.0f;
}

[[nodiscard]] Corpus load_corpus() {
    Corpus corpus;
    for (ucd::Label &label : ucd::descriptive_labels()) {
        corpus.code_points.push_back(label.cp);
        corpus.weights.push_back(label_weight(label.kind));
        corpus.texts.push_back(std::move(label.text));
    }
    return corpus;
}

[[nodiscard]] std::expected<VectorIndex, Error>
build_index(const Embedder &embedder, Corpus corpus, const std::function<void(std::string_view)> &notify) {
    const std::size_t count = corpus.texts.size();
    // Texts of similar length share a batch, which keeps padding small.
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});
    std::ranges::stable_sort(order, {}, [&corpus](const std::size_t i) { return corpus.texts[i].size(); });

    std::vector<float> rows(count * kEmbeddingDim);
    std::size_t next_report = count / 4;
    for (std::size_t begin = 0; begin < count; begin += kBuildBatch) {
        const std::size_t end = std::min(count, begin + kBuildBatch);
        std::vector<std::string_view> batch;
        for (std::size_t i = begin; i < end; ++i) {
            batch.emplace_back(corpus.texts[order[i]]);
        }
        const std::expected<std::vector<float>, Error> vectors = embedder.embed(batch);
        if (!vectors) {
            return std::unexpected(vectors.error());
        }
        for (std::size_t i = begin; i < end; ++i) {
            std::copy_n(vectors->begin() + static_cast<std::ptrdiff_t>((i - begin) * kEmbeddingDim), kEmbeddingDim,
                        rows.begin() + static_cast<std::ptrdiff_t>(order[i] * kEmbeddingDim));
        }
        if (end >= next_report && end < count) {
            tell(notify, "semantic index: " + std::to_string(end * 100 / count) + "%");
            next_report += count / 4;
        }
    }
    return VectorIndex::create(std::move(corpus.code_points), std::move(rows), kEmbeddingDim);
}

[[nodiscard]] std::uint64_t size_of(const std::filesystem::path &file) {
    std::error_code ec;
    const std::uintmax_t size = std::filesystem::file_size(file, ec);
    return ec ? 0 : size;
}

}  // namespace

bool built_in() { return true; }

std::expected<std::vector<Match>, Error>
search(const std::string_view query, const std::size_t limit, const std::function<void(std::string_view)> &notify) {
    const std::expected<std::filesystem::path, Error> dir = data_dir();
    if (!dir) {
        return std::unexpected(dir.error());
    }
    const std::filesystem::path model_path = *dir / "model.onnx";
    const std::filesystem::path vocab_path = *dir / "vocab.txt";
    const std::expected<std::string, Error> vocab = read_whole_file(vocab_path);
    if (!vocab) {
        return std::unexpected(vocab.error());
    }
    if (!std::filesystem::is_regular_file(model_path)) {
        return std::unexpected(missing_file(model_path));
    }
    std::expected<WordPiece, std::string> tokenizer = WordPiece::create(*vocab);
    if (!tokenizer) {
        return std::unexpected(Error{.message = vocab_path.string() + " is not a usable vocabulary: " + tokenizer.error()});
    }
    if (limit == 0 || tokenizer->encode(query).size() <= 2) {
        return std::vector<Match>{};  // nothing to describe
    }

    Corpus corpus = load_corpus();
    const std::vector<float> weights = corpus.weights;
    const IndexKey key{.dim = kEmbeddingDim,
                       .count = corpus.texts.size(),
                       .model_size = size_of(model_path),
                       .vocab_size = vocab->size(),
                       .corpus_hash = corpus_hash(corpus.code_points, corpus.texts),
                       .unicode_version = std::string(ucd::unicode_version())};
    const std::expected<std::filesystem::path, Error> cache = index_cache_path();
    if (!cache) {
        return std::unexpected(cache.error());
    }

    std::expected<VectorIndex, IndexProblem> cached = IndexFile::read(*cache, key);
    if (!cached) {
        // Fail before the slow build when the index cannot be stored.
        std::error_code ec;
        std::filesystem::create_directories(cache->parent_path(), ec);
        if (ec) {
            return std::unexpected(Error{.message = "cannot create the semantic index directory " +
                                                    cache->parent_path().string() + ": " + ec.message() +
                                                    " (set XDG_CACHE_HOME to a writable directory)"});
        }
    }
    const bool must_build = !cached.has_value();
    std::expected<Embedder, Error> embedder =
        Embedder::create(model_path, std::move(*tokenizer), must_build ? kBuildThreads : kQueryThreads);
    if (!embedder) {
        return std::unexpected(embedder.error());
    }

    std::optional<VectorIndex> index;
    if (cached) {
        index = std::move(*cached);
    } else {
        tell(notify, cached.error().missing ? "building the semantic index (first use, a few seconds)"
                                            : "rebuilding the semantic index: " + cached.error().message);
        const auto started = std::chrono::steady_clock::now();
        std::expected<VectorIndex, Error> built = build_index(*embedder, std::move(corpus), notify);
        if (!built) {
            return std::unexpected(built.error());
        }
        if (const std::expected<void, Error> written = IndexFile::write(*cache, key, *built); !written) {
            return std::unexpected(written.error());
        }
        const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
        char text[96];
        std::snprintf(text, sizeof text, "semantic index built in %.1f s", seconds);
        tell(notify, text);
        index = std::move(*built);
    }

    const std::string_view texts[1] = {query};
    const std::expected<std::vector<float>, Error> vector = embedder->embed(texts);
    if (!vector) {
        return std::unexpected(vector.error());
    }
    return index->search(*vector, limit, weights);
}

}  // namespace tellmeutf::semantic
