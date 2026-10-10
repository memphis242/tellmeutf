// Embeddings from the C++ pipeline against reference vectors from
// onnxruntime-Python. Skips (exit status 77) when the model files are absent.
#include "check.hpp"
#include "semantic_embedder.hpp"
#include "semantic_paths.hpp"
#include "wordpiece.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace sem = tellmeutf::semantic;
namespace fs = std::filesystem;

int main() {
    const auto dir = sem::data_dir();
    if (!dir || !fs::exists(*dir / "model.onnx") || !fs::exists(*dir / "vocab.txt")) {
        std::fprintf(stderr, "skipped: model files not found (run scripts/fetch-semantic.sh)\n");
        return 77;
    }
    std::ifstream vocab_in(*dir / "vocab.txt", std::ios::binary);
    std::ostringstream vocab;
    vocab << vocab_in.rdbuf();
    auto tokenizer = sem::WordPiece::create(vocab.str());
    CHECK(tokenizer.has_value());
    if (!tokenizer) {
        return tellmeutf::test::finish();
    }
    auto embedder = sem::Embedder::create(*dir / "model.onnx", std::move(*tokenizer), 2);
    CHECK(embedder.has_value());
    if (!embedder) {
        std::fprintf(stderr, "%s\n", embedder.error().message.c_str());
        return tellmeutf::test::finish();
    }

    std::ifstream golden(fs::path(TELLMEUTF_SOURCE_DIR) / "tests/data/embedding_golden.tsv");
    std::string line;
    int cases = 0;
    while (std::getline(golden, line)) {
        const std::size_t tab = line.find('\t');
        const std::string text = line.substr(0, tab);
        std::istringstream numbers(line.substr(tab + 1));
        std::vector<float> reference;
        float value = 0;
        while (numbers >> value) {
            reference.push_back(value);
        }
        CHECK(reference.size() == sem::kEmbeddingDim);
        const std::string_view texts[1] = {text};
        const auto vectors = embedder->embed(texts);
        CHECK(vectors.has_value());
        if (!vectors || reference.size() != sem::kEmbeddingDim) {
            continue;
        }
        double cosine = 0;
        double norm = 0;
        for (std::size_t i = 0; i < reference.size(); ++i) {
            cosine += static_cast<double>((*vectors)[i]) * static_cast<double>(reference[i]);
            norm += static_cast<double>((*vectors)[i]) * static_cast<double>((*vectors)[i]);
        }
        if (cosine < 0.999 || norm < 0.999 || norm > 1.001) {
            std::fprintf(stderr, "'%s': cosine %.6f, norm %.6f\n", text.c_str(), cosine, norm);
        }
        CHECK(cosine >= 0.999);
        CHECK(norm > 0.999 && norm < 1.001);
        ++cases;
    }
    CHECK(cases >= 5);

    // A batch gives nearly the same vectors as single runs (the model quantizes
    // activations per tensor, so the padding and the other rows shift them slightly).
    const std::string_view batch[3] = {"heavy check mark", "a", "pile of poo"};
    const auto together = embedder->embed(batch);
    CHECK(together.has_value() && together->size() == 3 * sem::kEmbeddingDim);
    for (std::size_t i = 0; i < 3 && together; ++i) {
        const std::string_view one[1] = {batch[i]};
        const auto alone = embedder->embed(one);
        CHECK(alone.has_value());
        double cosine = 0;
        for (std::size_t d = 0; alone && d < sem::kEmbeddingDim; ++d) {
            cosine += static_cast<double>((*together)[i * sem::kEmbeddingDim + d]) * static_cast<double>((*alone)[d]);
        }
        CHECK(cosine > 0.97);
    }

    // A file that is not a model is an Error, not an exception.
    auto vocab_tokenizer = sem::WordPiece::create(vocab.str());
    CHECK(vocab_tokenizer.has_value());
    CHECK(!sem::Embedder::create(*dir / "vocab.txt", std::move(*vocab_tokenizer), 1).has_value());
    return tellmeutf::test::finish();
}
