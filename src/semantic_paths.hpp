// Where the semantic search keeps its model files and its index.
#pragma once

#include "semantic.hpp"

#include <expected>
#include <filesystem>

namespace tellmeutf::semantic {

// Directory with model.onnx and vocab.txt: $TELLMEUTF_SEMANTIC_DIR if set and
// not empty, else ${XDG_DATA_HOME:-$HOME/.local/share}/tellmeutf/semantic.
// Fails when neither the override nor a home directory is available.
[[nodiscard]] std::expected<std::filesystem::path, Error> data_dir();

// Index cache file: ${XDG_CACHE_HOME:-$HOME/.cache}/tellmeutf/semantic-index.bin.
[[nodiscard]] std::expected<std::filesystem::path, Error> index_cache_path();

}  // namespace tellmeutf::semantic
