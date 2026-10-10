// tellmeutf: wiring of option parsing, argument interpretation, lookup and output.
#include "options.hpp"
#include "query.hpp"
#include "render.hpp"
#include "ucd.hpp"
#include "utf8.hpp"

#include <unistd.h>

#include <algorithm>
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

// Resolves a name lookup. Returns the exit status of this lookup.
int resolve_name(Output &out, const std::string &name, const struct tellmeutf::options::Options &options) {
    std::vector<char32_t> matches = ucd::find_exact(name);
    if (matches.empty()) {
        matches = ucd::search(name);
    }
    if (matches.empty()) {
        report("no character matches '" + tellmeutf::utf8::escape_for_message(name) + "'; try fewer or shorter words, or give a code point such as U+276F");
        return kNotFound;
    }
    if (matches.size() == 1) {
        show_window(out, matches.front(), options);
        return kOk;
    }

    const std::size_t shown = std::min<std::size_t>(matches.size(), options.max_candidates);
    std::vector<ucd::CharInfo> rows;
    rows.reserve(shown);
    for (std::size_t i = 0; i < shown; ++i) {
        rows.push_back(ucd::lookup(matches[i]));
    }
    out.write_block(tellmeutf::render::render_block(rows, std::nullopt, {}));
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
