// Facade for builds configured without -DTELLMEUTF_SEMANTIC=ON.
#include "semantic.hpp"

namespace tellmeutf::semantic {

bool built_in() { return false; }

std::expected<std::vector<Match>, Error>
search(const std::string_view, const std::size_t, const std::function<void(std::string_view)> &) {
    return std::unexpected(Error{
        .message = "this build has no semantic search; configure with -DTELLMEUTF_SEMANTIC=ON after running "
                   "scripts/fetch-semantic.sh (see README)"});
}

}  // namespace tellmeutf::semantic
