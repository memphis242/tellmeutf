// Semantic search: finds code points by a free-text description with a small
// sentence-embedding model run locally (docs/design.md, "Semantic search").
//
// The facade exists in every build. Without -DTELLMEUTF_SEMANTIC=ON, search()
// always fails with an explanatory Error.
#pragma once

#include <cstddef>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace tellmeutf::semantic {

struct Match {
    char32_t cp = 0;
    float score = 0.0f;  // cosine similarity
};

struct Error {
    std::string message;  // plain and actionable
};

// False in builds configured without -DTELLMEUTF_SEMANTIC=ON.
[[nodiscard]] bool built_in();

// Most similar code points to a free-text description, best first, at most
// `limit`, each code point once. `notify` receives one-line progress messages
// (for example while the index is built the first time); it may be empty.
[[nodiscard]] std::expected<std::vector<Match>, Error>
search(std::string_view query, std::size_t limit, const std::function<void(std::string_view)> &notify);

}  // namespace tellmeutf::semantic
