// The semantic search facade end to end, with the real model. Environment
// overrides keep it away from the user's directories. Skips (exit status 77)
// when the model files are absent.
#include "check.hpp"
#include "semantic.hpp"
#include "semantic_paths.hpp"

#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sem = tellmeutf::semantic;
namespace fs = std::filesystem;

namespace {

bool in_top(const std::vector<sem::Match> &matches, const std::vector<char32_t> &wanted, const std::size_t n) {
    for (std::size_t i = 0; i < std::min(n, matches.size()); ++i) {
        if (std::ranges::find(wanted, matches[i].cp) != wanted.end()) {
            return true;
        }
    }
    return false;
}

void test_without_model_files(const fs::path &scratch) {
    const char *const previous = std::getenv("TELLMEUTF_SEMANTIC_DIR");
    const std::string saved = previous != nullptr ? previous : "";
    const fs::path empty = scratch / "empty-model-dir";
    fs::create_directories(empty);
    setenv("TELLMEUTF_SEMANTIC_DIR", empty.c_str(), 1);
    const auto result = sem::search("poop", 5, {});
    CHECK(!result.has_value());
    if (!result) {
        CHECK(result.error().message.find("fetch-semantic.sh") != std::string::npos);
        CHECK(result.error().message.find("vocab.txt") != std::string::npos);
    }
    if (previous != nullptr) {
        setenv("TELLMEUTF_SEMANTIC_DIR", saved.c_str(), 1);
    } else {
        unsetenv("TELLMEUTF_SEMANTIC_DIR");
    }
}

}  // namespace

int main() {
    CHECK(sem::built_in());
    const fs::path scratch = fs::temp_directory_path() / ("tellmeutf-semantic-test-" + std::to_string(::getpid()));
    fs::create_directories(scratch);
    test_without_model_files(scratch);

    const auto dir = sem::data_dir();
    if (!dir || !fs::exists(*dir / "model.onnx") || !fs::exists(*dir / "vocab.txt")) {
        std::fprintf(stderr, "skipped: model files not found (run scripts/fetch-semantic.sh)\n");
        std::error_code ec;
        fs::remove_all(scratch, ec);
        return tellmeutf::test::failure_count() == 0 ? 77 : 1;
    }

    // An unwritable cache location fails before the slow build.
    {
        const fs::path file = scratch / "a-file";
        std::ofstream(file) << "x";
        setenv("XDG_CACHE_HOME", (file / "sub").c_str(), 1);
        const auto result = sem::search("poop", 5, {});
        CHECK(!result.has_value());
        if (!result) {
            CHECK(result.error().message.find("XDG_CACHE_HOME") != std::string::npos);
        }
    }

    const fs::path cache = scratch / "cache";
    setenv("XDG_CACHE_HOME", cache.c_str(), 1);
    std::vector<std::string> messages;
    const auto notify = [&messages](const std::string_view message) { messages.emplace_back(message); };

    const auto poop = sem::search("poop", 5, notify);
    CHECK(poop.has_value());
    CHECK(!messages.empty());  // the first call builds the index and says so
    CHECK(fs::exists(cache / "tellmeutf" / "semantic-index.bin"));
    if (poop) {
        CHECK(poop->size() == 5);
        CHECK(in_top(*poop, {0x1F4A9}, 5));
        for (std::size_t i = 1; i < poop->size(); ++i) {
            CHECK((*poop)[i - 1].score >= (*poop)[i].score);
            CHECK((*poop)[i - 1].cp != (*poop)[i].cp);
        }
    }

    messages.clear();
    const auto again = sem::search("trash can", 5, notify);
    CHECK(again.has_value());
    CHECK(messages.empty());  // the cached index was reused
    if (again) {
        CHECK(in_top(*again, {0x1F5D1}, 5));
    }

    const auto check_query = [](const char *const query, const std::vector<char32_t> &wanted) {
        const auto result = sem::search(query, 5, {});
        CHECK(result.has_value());
        if (result) {
            if (!in_top(*result, wanted, 5)) {
                std::fprintf(stderr, "'%s': expected code point missing from the top 5\n", query);
            }
            CHECK(in_top(*result, wanted, 5));
        }
    };
    check_query("sad face", {0x2639, 0x1F641, 0x1F622, 0x1F61E});
    check_query("degrees", {0x00B0});
    check_query("music note", {0x266A, 0x266B});

    // Degenerate queries give an empty result, not an error.
    const auto blank = sem::search("   ", 5, {});
    CHECK(blank.has_value() && blank->empty());
    const auto none = sem::search("poop", 0, {});
    CHECK(none.has_value() && none->empty());

    // A damaged cache is rebuilt, with a message saying why.
    {
        std::ofstream(cache / "tellmeutf" / "semantic-index.bin", std::ios::trunc) << "garbage";
        messages.clear();
        const auto rebuilt = sem::search("poop", 3, notify);
        CHECK(rebuilt.has_value());
        CHECK(!messages.empty());
        if (rebuilt) {
            CHECK(in_top(*rebuilt, {0x1F4A9}, 3));
        }
    }

    std::error_code ec;
    fs::remove_all(scratch, ec);
    return tellmeutf::test::finish();
}
