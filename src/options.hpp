// Command-line option parsing and the usage text.
#pragma once

#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <vector>

namespace tellmeutf::options {

inline constexpr std::uint32_t kDefaultContext = 2;
inline constexpr std::uint32_t kMaxContext = 1114111;
inline constexpr std::uint32_t kDefaultMaxCandidates = 20;
inline constexpr std::uint32_t kMaxMaxCandidates = 100000;

enum class Action { Lookup, Help, Version };

struct Options {
    std::uint32_t context = kDefaultContext;         // rows before and after the target
    std::uint32_t max_candidates = kDefaultMaxCandidates;
    bool force_name = false;
};

struct CommandLine {
    Action action = Action::Lookup;
    struct Options options;
    // Positional arguments in order (empty for Help and Version).
    std::vector<std::string> arguments;
};

struct UsageError {
    // Plain message that says what is wrong and what to do about it.
    std::string message;
    // True when the full usage text is the useful remedy (no arguments given).
    bool show_usage = false;
};

// Parses the arguments after the program name. A help or version request is
// returned as an Action as soon as it is met, ahead of any later problem.
[[nodiscard]] std::expected<CommandLine, UsageError> parse(const std::vector<std::string> &args);

// Usage and help text, ending in a newline.
[[nodiscard]] std::string_view usage_text();

}  // namespace tellmeutf::options
