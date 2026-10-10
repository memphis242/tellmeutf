// Sentence embedder: WordPiece tokenizer plus the ONNX model, with mean
// pooling and unit-length normalization. Only built with -DTELLMEUTF_SEMANTIC=ON.
#pragma once

#include "semantic.hpp"
#include "wordpiece.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace tellmeutf::semantic {

// Embedding size of all-MiniLM-L6-v2.
inline constexpr std::uint32_t kEmbeddingDim = 384;

class Embedder {
public:
    // Loads the model with `threads` intra-op threads. Fails when the file
    // cannot be loaded or does not have the expected inputs and output size.
    [[nodiscard]] static std::expected<Embedder, Error> create(const std::filesystem::path &model_path,
                                                               WordPiece tokenizer, int threads);

    Embedder(Embedder &&) noexcept;
    Embedder &operator=(Embedder &&) noexcept;
    ~Embedder();

    // One unit-length vector of kEmbeddingDim floats per text, concatenated
    // (texts.size() * kEmbeddingDim floats), in one model run.
    [[nodiscard]] std::expected<std::vector<float>, Error> embed(std::span<const std::string_view> texts) const;

private:
    struct Impl;
    explicit Embedder(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> impl_;
};

}  // namespace tellmeutf::semantic
