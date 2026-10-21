#include "check.hpp"
#include "semantic.hpp"

#include <string_view>

int main() {
    CHECK(!tellmeutf::semantic::built_in());

    const std::expected<std::vector<tellmeutf::semantic::Match>, tellmeutf::semantic::Error> result =
        tellmeutf::semantic::search("a smiling face", 5, {});
    CHECK(!result);
    if (!result) {
        CHECK(result.error().message.find("this build has no semantic search") != std::string::npos);
        CHECK(result.error().message.find("scripts/fetch-semantic.sh") != std::string::npos);
    }

    return tellmeutf::test::finish();
}
