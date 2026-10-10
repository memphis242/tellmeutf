#include "options.hpp"

#include "utf8.hpp"

#include <cstddef>
#include <optional>
#include <utility>

namespace tellmeutf::options {

namespace {

constexpr std::string_view kUsage =
    "Usage: tellmeutf [options] <character | code point | name>...\n"
    "\n"
    "Show the UTF-32 value, the character and the name of a Unicode character,\n"
    "together with its neighbors.\n"
    "\n"
    "Arguments:\n"
    "  a character          the characters are looked up one by one (\xE2\x9D\xAF)\n"
    "  a code point         U+276F, 0x276f, \\u276f, or bare hex digits such as 276f\n"
    "  a name               snowman, \"check mark\", heavy check mark, nbsp\n"
    "\n"
    "Options:\n"
    "  -C N, --context=N    characters to show before and after (default 2)\n"
    "  -s, --single         show only the result (same as -C 0)\n"
    "  -n, --name           read the arguments as a name, even if they look like hex\n"
    "  -m N, --max=N        candidates to list for an ambiguous name (default 20)\n"
    "  -e, --exact          match names strictly; no approximate fallback for typos\n"
    "  -h, --help           show this help\n"
    "  -V, --version        show the version\n"
    "  --                   end of options\n"
    "\n"
    "Examples:\n"
    "  tellmeutf \xE2\x9D\xAF              look up a character\n"
    "  tellmeutf U+276F         look up a code point\n"
    "  tellmeutf 276f -C 4      four neighbors on each side\n"
    "  tellmeutf -s heavy check mark\n"
    "  tellmeutf --name face    read 'face' as a name, not as hex digits\n"
    "\n"
    "A name with no strict match is matched approximately (typos, run-together\n"
    "words, plurals, extra words): a clear winner is shown with a note on stderr,\n"
    "otherwise the closest matches are listed. --exact turns this off.\n"
    "\n"
    "Exit status: 0 success, 1 name not found, 2 usage error, 3 ambiguous name\n"
    "(or only approximate matches), 4 could not write the output.\n";

// Options that can be written in short or long form.
enum class Opt { Context, Single, Name, Max, Exact, Help, Version };

struct OptionSpec {
    Opt opt;
    bool takes_value;
};

std::optional<OptionSpec> short_spec(const char c) {
    switch (c) {
    case 'C': return OptionSpec{Opt::Context, true};
    case 's': return OptionSpec{Opt::Single, false};
    case 'n': return OptionSpec{Opt::Name, false};
    case 'm': return OptionSpec{Opt::Max, true};
    case 'e': return OptionSpec{Opt::Exact, false};
    case 'h': return OptionSpec{Opt::Help, false};
    case 'V': return OptionSpec{Opt::Version, false};
    default: return std::nullopt;
    }
}

std::optional<OptionSpec> long_spec(const std::string_view name) {
    if (name == "context") return OptionSpec{Opt::Context, true};
    if (name == "single") return OptionSpec{Opt::Single, false};
    if (name == "name") return OptionSpec{Opt::Name, false};
    if (name == "max") return OptionSpec{Opt::Max, true};
    if (name == "exact") return OptionSpec{Opt::Exact, false};
    if (name == "help") return OptionSpec{Opt::Help, false};
    if (name == "version") return OptionSpec{Opt::Version, false};
    return std::nullopt;
}

UsageError error(std::string message) { return UsageError{.message = std::move(message), .show_usage = false}; }

// Parses a non-negative decimal integer within [min, max].
std::expected<std::uint32_t, UsageError> parse_number(const std::string_view option, const std::string_view text,
                                                      const std::uint32_t min, const std::uint32_t max) {
    const std::string range = std::to_string(min) + " to " + std::to_string(max);
    const auto bad = [&] {
        return std::unexpected(error("option " + std::string(option) + " needs a whole number from " + range +
                                     ", not '" + utf8::escape_for_message(text) + "'"));
    };
    if (text.empty()) {
        return bad();
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        if (c < '0' || c > '9') {
            return bad();
        }
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
        if (value > max) {
            return bad();
        }
    }
    if (value < min) {
        return bad();
    }
    return static_cast<std::uint32_t>(value);
}

// Accumulates the effect of options as they are met.
class Parser {
public:
    explicit Parser(const std::vector<std::string> &args) : args_(args) {}

    std::expected<CommandLine, UsageError> run() {
        bool options_ended = false;
        for (index_ = 0; index_ < args_.size(); ++index_) {
            const std::string &arg = args_[index_];
            if (options_ended || arg.size() < 2 || arg[0] != '-') {
                result_.arguments.push_back(arg);
            } else if (arg == "--") {
                options_ended = true;
            } else if (const auto outcome = arg[1] == '-' ? parse_long(arg) : parse_short_cluster(arg); !outcome) {
                return std::unexpected(outcome.error());
            } else if (*outcome) {
                return std::move(result_);
            }
        }
        if (result_.arguments.empty()) {
            return std::unexpected(UsageError{
                .message = "no character, code point or name given; pass at least one", .show_usage = true});
        }
        return std::move(result_);
    }

private:
    // Result: true when parsing ends early (help or version).
    using Outcome = std::expected<bool, UsageError>;

    // Applies one option; `value` is its argument when it takes one.
    Outcome apply(const std::string_view shown, const struct OptionSpec spec, const std::string_view value) {
        switch (spec.opt) {
        case Opt::Context: {
            const auto n = parse_number(shown, value, 0, kMaxContext);
            if (!n) return std::unexpected(n.error());
            result_.options.context = *n;
            return false;
        }
        case Opt::Max: {
            const auto n = parse_number(shown, value, 1, kMaxMaxCandidates);
            if (!n) return std::unexpected(n.error());
            result_.options.max_candidates = *n;
            return false;
        }
        case Opt::Single:
            result_.options.context = 0;
            return false;
        case Opt::Name:
            result_.options.force_name = true;
            return false;
        case Opt::Exact:
            result_.options.exact_only = true;
            return false;
        case Opt::Help:
            result_.action = Action::Help;
            result_.arguments.clear();
            return true;
        case Opt::Version:
            result_.action = Action::Version;
            result_.arguments.clear();
            return true;
        }
        return false;
    }

    // Takes the value from the next argument.
    std::expected<std::string_view, UsageError> next_value(const std::string_view shown) {
        if (index_ + 1 >= args_.size()) {
            return std::unexpected(error("option " + std::string(shown) + " needs a value"));
        }
        return std::string_view(args_[++index_]);
    }

    Outcome parse_long(const std::string &arg) {
        const std::string_view body = std::string_view(arg).substr(2);
        const std::size_t equals = body.find('=');
        const std::string_view name = body.substr(0, equals);
        const auto spec = long_spec(name);
        if (!spec) {
            return std::unexpected(error("unknown option '--" + utf8::escape_for_message(name) + "'; see --help for the options"));
        }
        const std::string shown = "--" + std::string(name);
        if (!spec->takes_value) {
            if (equals != std::string_view::npos) {
                return std::unexpected(error("option " + shown + " takes no value"));
            }
            return apply(shown, *spec, {});
        }
        if (equals != std::string_view::npos) {
            return apply(shown, *spec, body.substr(equals + 1));
        }
        const auto value = next_value(shown);
        if (!value) return std::unexpected(value.error());
        return apply(shown, *spec, *value);
    }

    // Handles "-s", "-C2", "-C 2", and clusters such as "-sn" or "-sC2".
    Outcome parse_short_cluster(const std::string &arg) {
        for (std::size_t i = 1; i < arg.size(); ++i) {
            const auto spec = short_spec(arg[i]);
            if (!spec) {
                const std::string unknown = "-" + utf8::escape_for_message(std::string_view(arg).substr(i));
                return std::unexpected(error("unknown option '" + unknown + "'; see --help for the options, "
                                             "or put -- before an argument that starts with a dash"));
            }
            const std::string shown = std::string("-") + arg[i];
            if (!spec->takes_value) {
                const auto outcome = apply(shown, *spec, {});
                if (!outcome || *outcome) return outcome;
                continue;
            }
            if (i + 1 < arg.size()) {
                return apply(shown, *spec, std::string_view(arg).substr(i + 1));
            }
            const auto value = next_value(shown);
            if (!value) return std::unexpected(value.error());
            return apply(shown, *spec, *value);
        }
        return false;
    }

    const std::vector<std::string> &args_;
    std::size_t index_ = 0;
    struct CommandLine result_;
};

}  // namespace

std::expected<CommandLine, UsageError> parse(const std::vector<std::string> &args) {
    return Parser(args).run();
}

std::string_view usage_text() { return kUsage; }

}  // namespace tellmeutf::options
