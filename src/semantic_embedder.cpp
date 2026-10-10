#include "semantic_embedder.hpp"

#include <onnxruntime_cxx_api.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>

namespace tellmeutf::semantic {

struct Embedder::Impl {
    Impl(const std::filesystem::path &model_path, WordPiece tokenizer_in, const int threads)
        : env(ORT_LOGGING_LEVEL_WARNING, "tellmeutf"),
          options(make_options(threads)),
          session(env, model_path.c_str(), options),
          tokenizer(std::move(tokenizer_in)) {}

    [[nodiscard]] static Ort::SessionOptions make_options(const int threads) {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(threads);
        options.SetInterOpNumThreads(1);
        return options;
    }

    Ort::Env env;
    Ort::SessionOptions options;
    Ort::Session session;
    WordPiece tokenizer;
    bool has_token_types = false;
    std::string output_name;
};

Embedder::Embedder(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}
Embedder::Embedder(Embedder &&) noexcept = default;
Embedder &Embedder::operator=(Embedder &&) noexcept = default;
Embedder::~Embedder() = default;

std::expected<Embedder, Error> Embedder::create(const std::filesystem::path &model_path, WordPiece tokenizer,
                                                const int threads) {
    assert(threads >= 1);
    try {
        auto impl = std::make_unique<Impl>(model_path, std::move(tokenizer), threads);
        Ort::AllocatorWithDefaultOptions allocator;
        bool has_ids = false;
        bool has_mask = false;
        for (std::size_t i = 0; i < impl->session.GetInputCount(); ++i) {
            const std::string name = impl->session.GetInputNameAllocated(i, allocator).get();
            has_ids = has_ids || name == "input_ids";
            has_mask = has_mask || name == "attention_mask";
            impl->has_token_types = impl->has_token_types || name == "token_type_ids";
        }
        if (!has_ids || !has_mask || impl->session.GetOutputCount() < 1) {
            return std::unexpected(Error{.message = "the model " + model_path.string() +
                                                    " does not look like a sentence-embedding BERT model "
                                                    "(expected inputs input_ids and attention_mask)"});
        }
        impl->output_name = impl->session.GetOutputNameAllocated(0, allocator).get();
        return Embedder(std::move(impl));
    } catch (const Ort::Exception &e) {
        return std::unexpected(Error{.message = "cannot load the model " + model_path.string() + ": " + e.what()});
    }
}

std::expected<std::vector<float>, Error> Embedder::embed(const std::span<const std::string_view> texts) const {
    Impl &impl = *impl_;  // Session::Run is not const-qualified in the C++ API, but is thread-safe
    const std::size_t batch = texts.size();
    if (batch == 0) {
        return std::vector<float>{};
    }
    std::vector<std::vector<std::int64_t>> encoded;
    encoded.reserve(batch);
    std::size_t length = 0;
    for (const std::string_view text : texts) {
        encoded.push_back(impl.tokenizer.encode(text));
        length = std::max(length, encoded.back().size());
    }
    std::vector<std::int64_t> ids(batch * length, impl.tokenizer.pad_id());
    std::vector<std::int64_t> mask(batch * length, 0);
    std::vector<std::int64_t> types(batch * length, 0);
    for (std::size_t row = 0; row < batch; ++row) {
        std::ranges::copy(encoded[row], ids.begin() + static_cast<std::ptrdiff_t>(row * length));
        std::fill_n(mask.begin() + static_cast<std::ptrdiff_t>(row * length), encoded[row].size(), std::int64_t{1});
    }

    try {
        const std::array<std::int64_t, 2> shape{static_cast<std::int64_t>(batch), static_cast<std::int64_t>(length)};
        const Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        const auto tensor = [&](std::vector<std::int64_t> &data) {
            return Ort::Value::CreateTensor<std::int64_t>(memory, data.data(), data.size(), shape.data(), shape.size());
        };
        std::vector<const char *> input_names{"input_ids", "attention_mask"};
        std::vector<Ort::Value> inputs;
        inputs.push_back(tensor(ids));
        inputs.push_back(tensor(mask));
        if (impl.has_token_types) {
            input_names.push_back("token_type_ids");
            inputs.push_back(tensor(types));
        }
        const std::array<const char *, 1> output_names{impl.output_name.c_str()};
        std::vector<Ort::Value> outputs = impl.session.Run(Ort::RunOptions{nullptr}, input_names.data(), inputs.data(),
                                                           inputs.size(), output_names.data(), output_names.size());
        const std::vector<std::int64_t> out_shape = outputs.at(0).GetTensorTypeAndShapeInfo().GetShape();
        if (out_shape.size() != 3 || out_shape[0] != static_cast<std::int64_t>(batch) ||
            out_shape[1] != static_cast<std::int64_t>(length) ||
            out_shape[2] != static_cast<std::int64_t>(kEmbeddingDim)) {
            return std::unexpected(Error{.message = "the model produced an unexpected output shape; this tool needs "
                                                    "all-MiniLM-L6-v2 (384 values per token)"});
        }
        const float *const hidden = outputs[0].GetTensorData<float>();

        std::vector<float> result(batch * kEmbeddingDim, 0.0f);
        for (std::size_t row = 0; row < batch; ++row) {
            float *const vec = result.data() + row * kEmbeddingDim;
            const std::size_t tokens = encoded[row].size();
            for (std::size_t t = 0; t < tokens; ++t) {
                const float *const h = hidden + (row * length + t) * kEmbeddingDim;
                for (std::size_t d = 0; d < kEmbeddingDim; ++d) {
                    vec[d] += h[d];
                }
            }
            float norm_sq = 0.0f;
            for (std::size_t d = 0; d < kEmbeddingDim; ++d) {
                vec[d] /= static_cast<float>(tokens);
                norm_sq += vec[d] * vec[d];
            }
            const float norm = std::sqrt(norm_sq);
            if (!std::isfinite(norm) || norm <= 0.0f) {
                return std::unexpected(Error{.message = "the model produced an invalid embedding"});
            }
            for (std::size_t d = 0; d < kEmbeddingDim; ++d) {
                vec[d] /= norm;
            }
        }
        return result;
    } catch (const Ort::Exception &e) {
        return std::unexpected(Error{.message = std::string("the embedding model failed: ") + e.what()});
    }
}

}  // namespace tellmeutf::semantic
