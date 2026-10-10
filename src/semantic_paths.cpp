#include "semantic_paths.hpp"

#include <cstdlib>
#include <optional>
#include <string>

namespace tellmeutf::semantic {

namespace {

// Value of an environment variable, unset and empty both meaning "no value".
[[nodiscard]] std::optional<std::string> env(const char *const name) {
    const char *const value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
}

// ${xdg_variable:-$HOME/<home_relative>}/tellmeutf/<leaf>
[[nodiscard]] std::expected<std::filesystem::path, Error>
xdg_path(const char *const xdg_variable, const char *const home_relative, const char *const leaf) {
    std::filesystem::path base;
    if (const std::optional<std::string> xdg = env(xdg_variable)) {
        base = *xdg;
    } else if (const std::optional<std::string> home = env("HOME")) {
        base = std::filesystem::path(*home) / home_relative;
    } else {
        return std::unexpected(Error{.message = std::string("cannot locate the semantic search files: neither ") +
                                                xdg_variable + " nor HOME is set"});
    }
    return base / "tellmeutf" / leaf;
}

}  // namespace

std::expected<std::filesystem::path, Error> data_dir() {
    if (const std::optional<std::string> dir = env("TELLMEUTF_SEMANTIC_DIR")) {
        return std::filesystem::path(*dir);
    }
    return xdg_path("XDG_DATA_HOME", ".local/share", "semantic");
}

std::expected<std::filesystem::path, Error> index_cache_path() {
    return xdg_path("XDG_CACHE_HOME", ".cache", "semantic-index.bin");
}

}  // namespace tellmeutf::semantic
