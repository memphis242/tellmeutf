#include "semantic_index.hpp"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <numeric>

namespace tellmeutf::semantic {

static_assert(std::endian::native == std::endian::little, "the index file format is little-endian");
static_assert(sizeof(char32_t) == 4 && sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

namespace {

constexpr std::array<char, 8> kMagic{'T', 'M', 'U', 'F', 'S', 'E', 'M', '\0'};
constexpr std::uint32_t kFormatVersion = 1;
constexpr std::size_t kHeaderSize = 64;
constexpr std::size_t kAlignment = 64;  // rows start on a multiple of this
constexpr std::size_t kVersionBytes = 16;
constexpr std::size_t kMaxDim = 4096;

[[nodiscard]] float dot(const float *const a, const float *const b, const std::size_t n) {
    // Eight independent partial sums let the compiler vectorize the loop.
    std::array<float, 8> lanes{};
    std::size_t i = 0;
    for (; i + 8 <= n; i += 8) {
        for (std::size_t j = 0; j < 8; ++j) {
            lanes[j] += a[i + j] * b[i + j];
        }
    }
    float sum = 0.0f;
    for (const float lane : lanes) {
        sum += lane;
    }
    for (; i < n; ++i) {
        sum += a[i] * b[i];
    }
    return sum;
}

[[nodiscard]] constexpr std::size_t round_up(const std::size_t value, const std::size_t multiple) {
    return (value + multiple - 1) / multiple * multiple;
}

// Offset of the first row for `count` code points.
[[nodiscard]] constexpr std::size_t rows_offset(const std::size_t count) {
    return round_up(kHeaderSize + count * sizeof(char32_t), kAlignment);
}

struct Owned {
    std::vector<char32_t> code_points;
    std::vector<float> rows;
};

class Mapping {
public:
    Mapping(void *const base, const std::size_t length) : base_(base), length_(length) {}
    Mapping(const Mapping &) = delete;
    Mapping &operator=(const Mapping &) = delete;
    ~Mapping() { ::munmap(base_, length_); }

private:
    void *base_;
    std::size_t length_;
};

void store(unsigned char *const header, const std::size_t offset, const std::uint64_t value, const std::size_t bytes) {
    std::memcpy(header + offset, &value, bytes);  // little-endian host
}

[[nodiscard]] std::uint64_t load(const unsigned char *const header, const std::size_t offset, const std::size_t bytes) {
    std::uint64_t value = 0;
    std::memcpy(&value, header + offset, bytes);
    return value;
}

[[nodiscard]] std::string errno_text() { return std::strerror(errno); }

}  // namespace

std::uint64_t corpus_hash(const std::span<const char32_t> code_points, const std::span<const std::string> texts) {
    assert(code_points.size() == texts.size());
    std::uint64_t hash = 14695981039346656037ULL;
    const auto mix = [&hash](const unsigned char byte) {
        hash ^= byte;
        hash *= 1099511628211ULL;
    };
    for (std::size_t i = 0; i < code_points.size(); ++i) {
        for (std::size_t shift = 0; shift < 32; shift += 8) {
            mix(static_cast<unsigned char>(static_cast<std::uint32_t>(code_points[i]) >> shift));
        }
        for (const char c : texts[i]) {
            mix(static_cast<unsigned char>(c));
        }
        mix(0);
    }
    return hash;
}

std::expected<VectorIndex, Error> VectorIndex::create(std::vector<char32_t> code_points, std::vector<float> rows,
                                                      const std::size_t dim) {
    if (dim == 0 || rows.size() / dim != code_points.size() || rows.size() % dim != 0) {
        return std::unexpected(Error{.message = "internal error: embedding rows do not match the code points"});
    }
    auto owned = std::make_shared<Owned>(Owned{.code_points = std::move(code_points), .rows = std::move(rows)});
    VectorIndex index;
    index.code_points_ = owned->code_points;
    index.rows_ = owned->rows;
    index.dim_ = dim;
    index.storage_ = std::move(owned);
    return index;
}

std::vector<Match> VectorIndex::search(const std::span<const float> query, const std::size_t limit,
                                       const std::span<const float> row_weights) const {
    assert(query.size() == dim_);
    assert(row_weights.empty() || row_weights.size() == size());
    const std::size_t count = size();
    if (limit == 0 || count == 0) {
        return {};
    }
    std::vector<float> scores(count);
    for (std::size_t i = 0; i < count; ++i) {
        const float similarity = dot(rows_.data() + i * dim_, query.data(), dim_);
        const float score = row_weights.empty() ? similarity : similarity * row_weights[i];
        // A NaN (from a damaged file) must not break the strict ordering below.
        scores[i] = std::isnan(score) ? -std::numeric_limits<float>::infinity() : score;
    }

    const auto better = [&scores](const std::size_t a, const std::size_t b) {
        return scores[a] != scores[b] ? scores[a] > scores[b] : a < b;
    };
    std::vector<std::size_t> order(count);
    std::iota(order.begin(), order.end(), std::size_t{0});

    // Sort a growing window of the best rows until it holds `limit` distinct
    // code points: every row outside the window scores no better than the
    // window's rows, so the first distinct code points in it are the answer.
    std::size_t window = std::min(count, std::max<std::size_t>(limit * 4, 64));
    for (;;) {
        std::partial_sort(order.begin(), order.begin() + static_cast<std::ptrdiff_t>(window), order.end(), better);
        std::vector<Match> matches;
        std::vector<char32_t> seen;
        for (std::size_t i = 0; i < window && matches.size() < limit; ++i) {
            const char32_t cp = code_points_[order[i]];
            if (std::ranges::find(seen, cp) == seen.end()) {
                seen.push_back(cp);
                matches.push_back(Match{.cp = cp, .score = scores[order[i]]});
            }
        }
        if (matches.size() == limit || window == count) {
            return matches;
        }
        window = std::min(count, window * 4);
    }
}

std::expected<void, Error> IndexFile::write(const std::filesystem::path &path, const IndexKey &key,
                                            const VectorIndex &index) {
    assert(key.count == index.size() && key.dim == index.dim());
    assert(key.unicode_version.size() < kVersionBytes);
    const auto fail = [&path](const std::string &what) {
        return std::unexpected(Error{.message = "cannot write the semantic index " + path.string() + ": " + what});
    };

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return fail("cannot create the directory (" + ec.message() + ")");
        }
    }
    std::filesystem::path temporary = path;
    temporary += ".tmp." + std::to_string(::getpid());

    std::array<unsigned char, kHeaderSize> header{};
    std::memcpy(header.data(), kMagic.data(), kMagic.size());
    store(header.data(), 8, kFormatVersion, 4);
    store(header.data(), 12, key.dim, 4);
    store(header.data(), 16, key.count, 8);
    store(header.data(), 24, key.model_size, 8);
    store(header.data(), 32, key.vocab_size, 8);
    store(header.data(), 40, key.corpus_hash, 8);
    std::memcpy(header.data() + 48, key.unicode_version.data(), key.unicode_version.size());

    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return fail("cannot create " + temporary.string() + " (" + errno_text() + ")");
        }
        const auto put = [&out](const void *const data, const std::size_t bytes) {
            out.write(static_cast<const char *>(data), static_cast<std::streamsize>(bytes));
        };
        put(header.data(), header.size());
        put(index.code_points().data(), index.code_points().size_bytes());
        const std::size_t padding = rows_offset(index.size()) - kHeaderSize - index.code_points().size_bytes();
        const std::array<char, kAlignment> zeros{};
        put(zeros.data(), padding);
        put(index.rows().data(), index.rows().size_bytes());
        out.close();
        if (!out) {
            std::filesystem::remove(temporary, ec);
            return fail("writing failed (" + errno_text() + ")");
        }
    }
    std::filesystem::rename(temporary, path, ec);
    if (ec) {
        const std::string message = ec.message();
        std::filesystem::remove(temporary, ec);
        return fail("cannot move the finished file into place (" + message + ")");
    }
    return {};
}

std::expected<VectorIndex, IndexProblem> IndexFile::read(const std::filesystem::path &path, const IndexKey &expected) {
    const auto unusable = [](const std::string &why) {
        return std::unexpected(IndexProblem{.missing = false, .message = why});
    };
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (errno == ENOENT) {
            return std::unexpected(IndexProblem{.missing = true, .message = "no index file yet"});
        }
        return unusable("cannot open " + path.string() + " (" + errno_text() + ")");
    }
    struct stat info {};
    if (::fstat(fd, &info) != 0 || !S_ISREG(info.st_mode)) {
        ::close(fd);
        return unusable(path.string() + " is not a regular file");
    }
    const std::size_t file_size = static_cast<std::size_t>(info.st_size);
    if (file_size < kHeaderSize) {
        ::close(fd);
        return unusable("the index file is truncated");
    }
    void *const base = ::mmap(nullptr, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    const std::string map_error = errno_text();
    ::close(fd);
    if (base == MAP_FAILED) {
        return unusable("cannot map the index file (" + map_error + ")");
    }
    auto mapping = std::make_shared<const Mapping>(base, file_size);

    const auto *const header = static_cast<const unsigned char *>(base);
    if (std::memcmp(header, kMagic.data(), kMagic.size()) != 0) {
        return unusable("the index file has the wrong signature");
    }
    if (load(header, 8, 4) != kFormatVersion) {
        return unusable("the index file has another format version");
    }
    if (load(header, 12, 4) != expected.dim || expected.dim == 0 || expected.dim > kMaxDim) {
        return unusable("the index was built with another embedding size");
    }
    if (load(header, 16, 8) != expected.count) {
        return unusable("the index was built for another number of names");
    }
    if (load(header, 24, 8) != expected.model_size) {
        return unusable("the index was built with another model file");
    }
    if (load(header, 32, 8) != expected.vocab_size) {
        return unusable("the index was built with another vocabulary file");
    }
    if (load(header, 40, 8) != expected.corpus_hash) {
        return unusable("the index was built from other names");
    }
    char version[kVersionBytes + 1] = {};
    std::memcpy(version, header + 48, kVersionBytes);
    if (expected.unicode_version != version) {
        return unusable("the index was built for another Unicode version");
    }

    // The counts are now known to equal the caller's, which come from compiled-in data.
    const std::size_t count = static_cast<std::size_t>(expected.count);
    const std::size_t dim = expected.dim;
    if (count > (std::numeric_limits<std::uint32_t>::max() / 4) ||
        file_size != rows_offset(count) + count * dim * sizeof(float)) {
        return unusable("the index file has the wrong size");
    }

    VectorIndex index;
    index.code_points_ = std::span<const char32_t>(static_cast<const char32_t *>(static_cast<const void *>(header + kHeaderSize)), count);
    index.rows_ = std::span<const float>(static_cast<const float *>(static_cast<const void *>(header + rows_offset(count))), count * dim);
    index.dim_ = dim;
    for (const char32_t cp : index.code_points_) {
        if (cp > 0x10FFFF) {
            return unusable("the index file holds an invalid code point");
        }
    }
    // mmap returns page-aligned memory and both offsets are multiples of 64.
    index.storage_ = std::move(mapping);
    return index;
}

}  // namespace tellmeutf::semantic
