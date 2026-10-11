// Embedding index: one unit-length vector per descriptive label, searched by
// brute force, with a validated on-disk form.
#pragma once

#include "semantic.hpp"

#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace tellmeutf::semantic {

// What an index file must have been built from to be usable now.
struct IndexKey {
    std::uint32_t dim = 0;
    std::uint64_t count = 0;
    std::uint64_t model_size = 0;  // bytes of model.onnx
    std::uint64_t vocab_size = 0;  // bytes of vocab.txt
    std::uint64_t corpus_hash = 0;  // see corpus_hash()
    std::string unicode_version;  // at most 15 bytes

    [[nodiscard]] bool operator==(const IndexKey &) const = default;
};

// FNV-1a over the code points and texts of the labels (in order).
[[nodiscard]] std::uint64_t corpus_hash(std::span<const char32_t> code_points, std::span<const std::string> texts);

// Row i of the index describes code point code_points()[i]; several rows may
// share a code point (a name and its aliases). Rows have `dim()` floats and
// are expected to be unit length, so a dot product is a cosine similarity.
// Immutable and cheap to copy.
class VectorIndex {
public:
    // Takes ownership of the data. Fails when the sizes disagree (rows.size()
    // must be code_points.size() * dim) or dim is 0.
    [[nodiscard]] static std::expected<VectorIndex, Error> create(std::vector<char32_t> code_points,
                                                                  std::vector<float> rows, std::size_t dim);

    [[nodiscard]] std::size_t size() const { return code_points_.size(); }
    [[nodiscard]] std::size_t dim() const { return dim_; }
    [[nodiscard]] std::span<const char32_t> code_points() const { return code_points_; }
    [[nodiscard]] std::span<const float> rows() const { return rows_; }

    // Best matches by dot product, best first, at most `limit`, each code
    // point once (its best row). Ties order by row. When `row_weights` is not
    // empty, each row's dot product is multiplied by its weight before ranking,
    // and Match::score is that product. Preconditions: query.size() == dim();
    // row_weights is empty or has size() entries.
    [[nodiscard]] std::vector<Match> search(std::span<const float> query, std::size_t limit,
                                            std::span<const float> row_weights = {}) const;

private:
    friend class IndexFile;
    VectorIndex() = default;

    std::shared_ptr<const void> storage_;  // keeps the spans below alive
    std::span<const char32_t> code_points_;
    std::span<const float> rows_;
    std::size_t dim_ = 0;
};

// Why an index file could not be used.
struct IndexProblem {
    bool missing = false;  // no file (as opposed to an unusable one)
    std::string message;
};

// Reading and writing of the on-disk form.
class IndexFile {
public:
    // Writes the index atomically (temporary file in the same directory, then
    // rename), creating parent directories. The key's count and dim must match
    // the index.
    [[nodiscard]] static std::expected<void, Error> write(const std::filesystem::path &path, const IndexKey &key,
                                                          const VectorIndex &index);

    // Maps the file read-only after checking magic, format version, key and
    // sizes against `expected`. Never trusts the file's own counts beyond
    // those checks.
    [[nodiscard]] static std::expected<VectorIndex, IndexProblem> read(const std::filesystem::path &path,
                                                                       const IndexKey &expected);
};

}  // namespace tellmeutf::semantic
