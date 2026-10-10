// Minimal test checks that stay active in every build type (unlike assert).
#pragma once

#include <cstdio>
#include <string_view>

namespace tellmeutf::test {

inline int &failure_count() {
    static int count = 0;
    return count;
}

inline void report_failure(const char *const file, const int line, const std::string_view what) {
    std::fprintf(stderr, "%s:%d: CHECK failed: %.*s\n", file, line,
                 static_cast<int>(what.size()), what.data());
    ++failure_count();
}

// Call at the end of main: prints a summary and returns the process status.
inline int finish() {
    const int failures = failure_count();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
    }
    return failures == 0 ? 0 : 1;
}

}  // namespace tellmeutf::test

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            ::tellmeutf::test::report_failure(__FILE__, __LINE__, #cond);    \
        }                                                                    \
    } while (false)
