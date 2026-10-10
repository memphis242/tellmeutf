#include "check.hpp"
#include "semantic_index.hpp"
#include "semantic_paths.hpp"

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace sem = tellmeutf::semantic;
namespace fs = std::filesystem;

namespace {

// Unit vector in the plane at `degrees`, padded with zeros to `dim` floats.
std::vector<float> direction(const double degrees, const std::size_t dim) {
    const double radians = degrees * 3.14159265358979323846 / 180.0;
    std::vector<float> v(dim, 0.0f);
    v[0] = static_cast<float>(std::cos(radians));
    v[1] = static_cast<float>(std::sin(radians));
    return v;
}

sem::VectorIndex make_index(const std::vector<char32_t> &cps, const std::vector<double> &degrees, const std::size_t dim = 4) {
    std::vector<float> rows;
    for (const double d : degrees) {
        const std::vector<float> v = direction(d, dim);
        rows.insert(rows.end(), v.begin(), v.end());
    }
    auto index = sem::VectorIndex::create(cps, rows, dim);
    CHECK(index.has_value());
    return std::move(*index);
}

void test_search_order_and_dedupe() {
    // Rows: cp A at 0 and 80 degrees, cp B at 30, cp C at 60, cp D at 90.
    const sem::VectorIndex index = make_index({0x41, 0x42, 0x43, 0x44, 0x41}, {0, 30, 60, 90, 80});
    const std::vector<float> query = direction(5, 4);
    const std::vector<sem::Match> top = index.search(query, 3);
    CHECK(top.size() == 3);
    CHECK(top[0].cp == 0x41 && top[1].cp == 0x42 && top[2].cp == 0x43);
    CHECK(std::fabs(top[0].score - static_cast<float>(std::cos(5.0 * 3.14159265358979323846 / 180.0))) < 1e-5f);
    CHECK(top[0].score > top[1].score && top[1].score > top[2].score);

    // A limit above the number of distinct code points returns each once.
    const std::vector<sem::Match> all = index.search(query, 50);
    CHECK(all.size() == 4);
    CHECK(index.search(query, 0).empty());
}

void test_duplicates_fill_the_window() {
    // Hundreds of near-identical rows of one code point must not crowd out the others.
    std::vector<char32_t> cps;
    std::vector<double> degrees;
    for (int i = 0; i < 300; ++i) {
        cps.push_back(0x41);
        degrees.push_back(i * 0.001);
    }
    cps.push_back(0x42);
    degrees.push_back(40);
    cps.push_back(0x43);
    degrees.push_back(60);
    const sem::VectorIndex index = make_index(cps, degrees);
    const std::vector<sem::Match> top = index.search(direction(0, 4), 3);
    CHECK(top.size() == 3);
    CHECK(top[0].cp == 0x41 && top[1].cp == 0x42 && top[2].cp == 0x43);
}

void test_ties_order_by_row() {
    const sem::VectorIndex index = make_index({0x50, 0x4F, 0x4E}, {10, 10, 10});
    const std::vector<sem::Match> top = index.search(direction(10, 4), 3);
    CHECK(top.size() == 3);
    CHECK(top[0].cp == 0x50 && top[1].cp == 0x4F && top[2].cp == 0x4E);
}

void test_create_rejects_bad_shapes() {
    CHECK(!sem::VectorIndex::create({0x41}, {1.0f, 0.0f, 0.0f}, 2).has_value());
    CHECK(!sem::VectorIndex::create({0x41}, {1.0f}, 0).has_value());
    CHECK(sem::VectorIndex::create({}, {}, 4).has_value());
}

sem::IndexKey key_for(const sem::VectorIndex &index) {
    return sem::IndexKey{.dim = static_cast<std::uint32_t>(index.dim()),
                         .count = index.size(),
                         .model_size = 123,
                         .vocab_size = 456,
                         .corpus_hash = 789,
                         .unicode_version = "18.0.0"};
}

void test_file_round_trip_and_validation(const fs::path &dir) {
    const sem::VectorIndex index = make_index({0x41, 0x42, 0x43}, {0, 45, 90}, 8);
    const sem::IndexKey key = key_for(index);
    const fs::path path = dir / "nested" / "index.bin";

    const auto missing = sem::IndexFile::read(path, key);
    CHECK(!missing.has_value() && missing.error().missing);

    CHECK(sem::IndexFile::write(path, key, index).has_value());
    const auto loaded = sem::IndexFile::read(path, key);
    CHECK(loaded.has_value());
    if (loaded) {
        CHECK(loaded->size() == 3 && loaded->dim() == 8);
        const std::vector<sem::Match> top = loaded->search(direction(50, 8), 2);
        CHECK(top.size() == 2 && top[0].cp == 0x42);
        CHECK(std::ranges::equal(loaded->rows(), index.rows()));
        CHECK(std::ranges::equal(loaded->code_points(), index.code_points()));
    }
    // No temporary files are left behind.
    int files = 0;
    for ([[maybe_unused]] const auto &entry : fs::directory_iterator(path.parent_path())) {
        ++files;
    }
    CHECK(files == 1);

    // A different key makes the file unusable (not missing).
    const auto expect_unusable = [&](const sem::IndexKey &other) {
        const auto result = sem::IndexFile::read(path, other);
        CHECK(!result.has_value() && !result.error().missing && !result.error().message.empty());
    };
    sem::IndexKey other = key;
    other.model_size = 1;
    expect_unusable(other);
    other = key;
    other.vocab_size = 1;
    expect_unusable(other);
    other = key;
    other.corpus_hash = 1;
    expect_unusable(other);
    other = key;
    other.unicode_version = "17.0.0";
    expect_unusable(other);
    other = key;
    other.count = 4;
    expect_unusable(other);
    other = key;
    other.dim = 4;
    expect_unusable(other);

    // Damaged files are reported, never crash.
    const auto damage = [&](const std::string &name, const auto &mutate) {
        const fs::path copy = dir / name;
        fs::copy_file(path, copy, fs::copy_options::overwrite_existing);
        mutate(copy);
        const auto result = sem::IndexFile::read(copy, key);
        CHECK(!result.has_value() && !result.error().missing);
    };
    damage("short.bin", [](const fs::path &p) { fs::resize_file(p, 10); });
    damage("truncated.bin", [](const fs::path &p) { fs::resize_file(p, fs::file_size(p) - 4); });
    damage("long.bin", [](const fs::path &p) { fs::resize_file(p, fs::file_size(p) + 64); });
    damage("magic.bin", [](const fs::path &p) {
        std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
        f.put('X');
    });
    damage("cp.bin", [](const fs::path &p) {
        std::fstream f(p, std::ios::in | std::ios::out | std::ios::binary);
        f.seekp(64 + 3);
        f.put('\x7f');  // code point above U+10FFFF
    });
    damage("empty.bin", [](const fs::path &p) { fs::resize_file(p, 0); });

    // A directory where the file should be is unusable, not a crash.
    const auto as_dir = sem::IndexFile::read(dir, key);
    CHECK(!as_dir.has_value() && !as_dir.error().missing);

    // An unwritable location is an Error that names the path.
    const fs::path blocked = dir / "short.bin" / "child" / "index.bin";  // parent is a regular file
    const auto failed = sem::IndexFile::write(blocked, key, index);
    CHECK(!failed.has_value() && failed.error().message.find("short.bin") != std::string::npos);
}

void test_corpus_hash() {
    const std::vector<char32_t> cps{0x41, 0x42};
    const std::vector<std::string> a{"ONE", "TWO"};
    const std::vector<std::string> b{"ONE", "TWO!"};
    const std::vector<std::string> c{"ONET", "WO"};
    CHECK(sem::corpus_hash(cps, a) == sem::corpus_hash(cps, a));
    CHECK(sem::corpus_hash(cps, a) != sem::corpus_hash(cps, b));
    CHECK(sem::corpus_hash(cps, a) != sem::corpus_hash(cps, c));
    CHECK(sem::corpus_hash(std::vector<char32_t>{0x42, 0x41}, a) != sem::corpus_hash(cps, a));
}

void test_paths() {
    setenv("TELLMEUTF_SEMANTIC_DIR", "/tmp/override", 1);
    CHECK(sem::data_dir().value() == "/tmp/override");
    unsetenv("TELLMEUTF_SEMANTIC_DIR");
    setenv("XDG_DATA_HOME", "/tmp/xdgdata", 1);
    setenv("XDG_CACHE_HOME", "/tmp/xdgcache", 1);
    CHECK(sem::data_dir().value() == "/tmp/xdgdata/tellmeutf/semantic");
    CHECK(sem::index_cache_path().value() == "/tmp/xdgcache/tellmeutf/semantic-index.bin");
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_CACHE_HOME");
    setenv("HOME", "/tmp/home", 1);
    CHECK(sem::data_dir().value() == "/tmp/home/.local/share/tellmeutf/semantic");
    CHECK(sem::index_cache_path().value() == "/tmp/home/.cache/tellmeutf/semantic-index.bin");
    unsetenv("HOME");
    CHECK(!sem::data_dir().has_value());
    CHECK(!sem::index_cache_path().has_value());
}

}  // namespace

int main() {
    test_search_order_and_dedupe();
    test_duplicates_fill_the_window();
    test_ties_order_by_row();
    test_create_rejects_bad_shapes();
    test_corpus_hash();
    test_paths();

    const fs::path dir = fs::temp_directory_path() / ("tellmeutf-index-test-" + std::to_string(::getpid()));
    fs::create_directories(dir);
    test_file_round_trip_and_validation(dir);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return tellmeutf::test::finish();
}
