// tellmeutf: wiring of option parsing, argument interpretation, lookup and output.
#include "options.hpp"
#include "query.hpp"
#include "render.hpp"
#include "semantic.hpp"
#include "ucd.hpp"
#include "utf8.hpp"

#include <unistd.h>

#include <algorithm>
#include <array>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#ifndef TELLMEUTF_VERSION
#error "TELLMEUTF_VERSION must be defined by the build"
#endif

namespace {

namespace ucd = tellmeutf::ucd;

enum ExitStatus : int {
    kOk = 0,
    kNotFound = 1,
    kUsage = 2,
    kAmbiguous = 3,
    kWriteFailed = 4,
    kSemanticUnavailable = 5,
};

void report(const std::string_view message) {
    std::fprintf(stderr, "tellmeutf: %.*s\n", static_cast<int>(message.size()), message.data());
}

bool stdout_supports_bold() {
    if (isatty(STDOUT_FILENO) == 0) {
        return false;
    }
    const char *const term = std::getenv("TERM");
    return term != nullptr && *term != '\0' && std::strcmp(term, "dumb") != 0;
}

// Writes stdout; remembers whether any write failed. SIGPIPE keeps its default
// action, so a reader that closes the pipe early (for example `| head -1`)
// ends the process quietly instead of reaching the write-failure report.
class Output {
public:
    explicit Output(const bool bold) : bold_(bold) {}

    [[nodiscard]] bool bold() const { return bold_; }

    // True once a write has failed; later writes are skipped.
    [[nodiscard]] bool failed() const { return failed_; }

    // Writes one block, preceded by a blank line when it is not the first.
    void write_block(const std::string &block) {
        if (written_any_block_) {
            write("\n");
        }
        write(block);
        written_any_block_ = true;
    }

    // Flushes and reports whether every write so far succeeded.
    [[nodiscard]] bool finish() {
        if (std::fflush(stdout) != 0) {
            failed_ = true;
            errno_ = errno;
        }
        return !failed_;
    }

    [[nodiscard]] std::string failure_reason() const { return std::strerror(errno_); }

private:
    void write(const std::string_view text) {
        if (failed_ || text.empty()) {
            return;
        }
        if (std::fwrite(text.data(), 1, text.size(), stdout) != text.size()) {
            failed_ = true;
            errno_ = errno;
        }
    }

    bool bold_;
    bool written_any_block_ = false;
    bool failed_ = false;
    int errno_ = 0;
};

// Code points target-context .. target+context, clipped to the Unicode range.
std::vector<ucd::CharInfo> window_around(const char32_t target, const std::uint32_t context) {
    assert(target <= ucd::kLastCodePoint);
    const char32_t first = target > context ? target - context : 0;
    const std::uint64_t last_wide = static_cast<std::uint64_t>(target) + context;
    const char32_t last = static_cast<char32_t>(std::min<std::uint64_t>(last_wide, ucd::kLastCodePoint));
    std::vector<ucd::CharInfo> rows;
    rows.reserve(last - first + 1);
    for (char32_t cp = first; cp <= last; ++cp) {
        rows.push_back(ucd::lookup(cp));
    }
    return rows;
}

void show_window(Output &out, const char32_t target, const struct tellmeutf::options::Options &options) {
    if (out.failed()) {
        return;
    }
    const std::vector<ucd::CharInfo> rows = window_around(target, options.context);
    const struct tellmeutf::render::BlockStyle style {
        .marker_column = options.context > 0,
        .bold_target = options.context > 0 && out.bold(),
    };
    const std::size_t target_index = target - rows.front().cp;
    out.write_block(tellmeutf::render::render_block(rows, target_index, style));
}

// "U+" and at least 4 uppercase hex digits.
std::string code_point_label(const char32_t cp) {
    std::array<char, 16> buffer{};
    const int length = std::snprintf(buffer.data(), buffer.size(), "U+%04X", static_cast<unsigned>(cp));
    assert(length > 0 && static_cast<std::size_t>(length) < buffer.size());
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}

// Rows for the first `count` code points of a candidate list.
std::vector<ucd::CharInfo> candidate_rows(const std::vector<char32_t> &code_points, const std::size_t count) {
    assert(count <= code_points.size());
    std::vector<ucd::CharInfo> rows;
    rows.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        rows.push_back(ucd::lookup(code_points[i]));
    }
    return rows;
}

// Handles a name with no strict match: shows a clearly best approximate match,
// or lists the closest ones. Returns the exit status of this lookup.
int resolve_approximate(Output &out, const std::string &name, const struct tellmeutf::options::Options &options) {
    const std::string shown_name = tellmeutf::utf8::escape_for_message(name);
    const std::vector<ucd::FuzzyMatch> matches = ucd::fuzzy_search(name, options.max_candidates);
    if (matches.empty()) {
        report("no character matches '" + shown_name + "'; try fewer or shorter words, or give a code point such as U+276F");
        return kNotFound;
    }
    if (ucd::is_confident(matches)) {
        const struct ucd::CharInfo best = ucd::lookup(matches.front().cp);
        show_window(out, best.cp, options);
        report("no exact match for '" + shown_name + "'; showing the closest: " + best.name + " (" +
               code_point_label(best.cp) + "). Use --exact to turn off approximate matching.");
        return kOk;
    }
    std::vector<char32_t> code_points;
    code_points.reserve(matches.size());
    for (const struct ucd::FuzzyMatch &match : matches) {
        code_points.push_back(match.cp);
    }
    out.write_block(tellmeutf::render::render_block(candidate_rows(code_points, code_points.size()), std::nullopt, {}));
    report("no exact match for '" + shown_name + "'; these are the closest matches.");
    return kAmbiguous;
}

// Resolves a name lookup. Returns the exit status of this lookup.
int resolve_name(Output &out, const std::string &name, const struct tellmeutf::options::Options &options) {
    std::vector<char32_t> matches = ucd::find_exact(name);
    if (matches.empty()) {
        matches = ucd::search(name);
    }
    if (matches.empty()) {
        if (!options.exact_only) {
            return resolve_approximate(out, name, options);
        }
        report("no character matches '" + tellmeutf::utf8::escape_for_message(name) + "'; try fewer or shorter words, or give a code point such as U+276F");
        return kNotFound;
    }
    if (matches.size() == 1) {
        show_window(out, matches.front(), options);
        return kOk;
    }

    const std::size_t shown = std::min<std::size_t>(matches.size(), options.max_candidates);
    out.write_block(tellmeutf::render::render_block(candidate_rows(matches, shown), std::nullopt, {}));
    std::string note = "'" + tellmeutf::utf8::escape_for_message(name) + "' matches " + std::to_string(matches.size()) + " characters";
    note += shown < matches.size() ? "; showing the first " + std::to_string(shown) : std::string("; showing all");
    note += ". Add more words to narrow it down";
    if (shown < matches.size()) {
        note += ", or raise --max to list more";
    }
    note += ".";
    report(note);
    return kAmbiguous;
}

// Matches below this cosine similarity are not shown. The floor only removes
// the weak tail of a ranking. Spike measurements with all-MiniLM-L6-v2: when
// the best match was right (56 of 84 queries) it scored 0.54 or more, 0.67 at
// the 10th percentile. Unrelated text peaked anywhere from 0.35 ("how do i
// install linux") to 0.71 ("pizza recipe", which does find a pizza), so no
// floor separates nonsense cleanly. Right answers that need outside knowledge
// ("tick" 0.37, "love" 0.32) sit just above 0.30, and a few hopeless queries
// ("mac command key" 0.07) fall below it.
constexpr float kMinSimilarity = 0.30f;

// Semantic lookup of a free-text description: a ranked list with the
// similarity before each row. Returns the exit status.
int resolve_semantic(Output &out, const std::string &description, const struct tellmeutf::options::Options &options) {
    // -s (context 0) asks for the best match only; other context values do not apply.
    const std::size_t limit = options.context == 0 ? 1 : options.max_candidates;
    const auto notify = [](const std::string_view message) {
        report(message);
        std::fflush(stderr);
    };
    const auto result = tellmeutf::semantic::search(description, limit, notify);
    if (!result) {
        report(result.error().message);
        return kSemanticUnavailable;
    }

    std::vector<char32_t> code_points;
    std::vector<std::string> prefixes;
    for (const struct tellmeutf::semantic::Match &match : *result) {
        if (match.score < kMinSimilarity) {
            break;  // best first, so everything after is lower too
        }
        std::array<char, 32> buffer{};
        const int length = std::snprintf(buffer.data(), buffer.size(), "%.2f  ", static_cast<double>(match.score));
        assert(length > 0 && static_cast<std::size_t>(length) < buffer.size());
        prefixes.emplace_back(buffer.data(), static_cast<std::size_t>(length));
        code_points.push_back(match.cp);
    }
    if (code_points.empty()) {
        report("nothing is semantically close to '" + tellmeutf::utf8::escape_for_message(description) +
               "'; try different words, or drop -S for name matching.");
        return kNotFound;
    }
    out.write_block(tellmeutf::render::render_block(candidate_rows(code_points, code_points.size()), std::nullopt, {}, prefixes));
    return kOk;
}

int run(const std::vector<std::string> &args) {
    const auto parsed = tellmeutf::options::parse(args);
    if (!parsed) {
        report(parsed.error().message);
        if (parsed.error().show_usage) {
            std::fputs(std::string(tellmeutf::options::usage_text()).c_str(), stderr);
        } else {
            std::fputs("Run 'tellmeutf --help' for usage.\n", stderr);
        }
        return kUsage;
    }

    switch (parsed->action) {
    case tellmeutf::options::Action::Help:
        std::fputs(std::string(tellmeutf::options::usage_text()).c_str(), stdout);
        break;
    case tellmeutf::options::Action::Version:
        std::printf("tellmeutf %s (Unicode %.*s)\n", TELLMEUTF_VERSION,
                    static_cast<int>(ucd::unicode_version().size()), ucd::unicode_version().data());
        break;
    case tellmeutf::options::Action::Lookup: {
        const auto lookups = tellmeutf::query::interpret(parsed->arguments, parsed->options.force_name);
        if (!lookups) {
            report(lookups.error().message);
            return kUsage;
        }
        Output out(stdout_supports_bold());
        int status = kOk;
        for (const struct tellmeutf::query::Lookup &lookup : *lookups) {
            if (out.failed()) {
                break;
            }
            int lookup_status = kOk;
            if (parsed->options.semantic) {
                assert(lookup.kind == tellmeutf::query::Lookup::Kind::Name);
                lookup_status = resolve_semantic(out, lookup.name, parsed->options);
                if (status == kOk) {
                    status = lookup_status;
                }
                continue;
            }
            switch (lookup.kind) {
            case tellmeutf::query::Lookup::Kind::CodePoint:
                show_window(out, lookup.code_point, parsed->options);
                break;
            case tellmeutf::query::Lookup::Kind::Name:
                lookup_status = resolve_name(out, lookup.name, parsed->options);
                break;
            }
            if (status == kOk) {
                status = lookup_status;
            }
        }
        if (!out.finish()) {
            report("could not write the output: " + out.failure_reason());
            return kWriteFailed;
        }
        return status;
    }
    }
    if (std::fflush(stdout) != 0) {
        report(std::string("could not write the output: ") + std::strerror(errno));
        return kWriteFailed;
    }
    return kOk;
}

}  // namespace

int main(const int argc, const char *const *const argv) {
    const std::vector<std::string> args =
        argc > 1 ? std::vector<std::string>(argv + 1, argv + argc) : std::vector<std::string>{};
    return run(args);
}
