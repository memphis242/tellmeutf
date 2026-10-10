// Placeholder implementation of ucd.hpp so dependents link; replaced by the real one.
#include "ucd.hpp"

namespace tellmeutf::ucd {

std::string_view category_code(const Category) { return "Lo"; }
CharInfo lookup(const char32_t cp) { return CharInfo{.cp = cp, .category = Category::Lo, .name = "PLACEHOLDER", .width = 1}; }
std::vector<char32_t> find_exact(const std::string_view) { return {}; }
std::vector<char32_t> search(const std::string_view) { return {}; }
std::string_view unicode_version() { return "0.0.0"; }

}  // namespace tellmeutf::ucd
