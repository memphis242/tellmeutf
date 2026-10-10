#include "check.hpp"
#include "options.hpp"

#include <string>
#include <vector>

namespace options = tellmeutf::options;

namespace {

using Args = std::vector<std::string>;

options::CommandLine ok(const Args &args) {
    const auto result = options::parse(args);
    CHECK(result.has_value());
    return result ? *result : options::CommandLine{};
}

bool fails(const Args &args) {
    const auto result = options::parse(args);
    CHECK(result.has_value() || !result.error().message.empty());
    return !result.has_value();
}

}  // namespace

int main() {
    // Defaults.
    {
        const auto cl = ok({"x"});
        CHECK(cl.action == options::Action::Lookup);
        CHECK(cl.options.context == 2);
        CHECK(cl.options.max_candidates == 20);
        CHECK(!cl.options.force_name);
        CHECK(cl.arguments == Args{"x"});
    }

    // Every spelling of the context option, before or after the arguments.
    for (const Args &args : {Args{"-C2", "x"}, Args{"-C", "2", "x"}, Args{"--context=2", "x"},
                             Args{"--context", "2", "x"}, Args{"x", "-C2"}, Args{"x", "--context", "2"}}) {
        const auto cl = ok(args);
        CHECK(cl.options.context == 2);
        CHECK(cl.arguments == Args{"x"});
    }
    CHECK(ok({"-C", "4", "x"}).options.context == 4);
    CHECK(ok({"-C0", "x"}).options.context == 0);
    CHECK(ok({"-C1114111", "x"}).options.context == 1114111);
    CHECK(ok({"-C", "007", "x"}).options.context == 7);

    // Single, name, max.
    CHECK(ok({"-s", "x"}).options.context == 0);
    CHECK(ok({"--single", "x"}).options.context == 0);
    CHECK(ok({"-C", "5", "-s", "x"}).options.context == 0);
    CHECK(ok({"-s", "-C", "5", "x"}).options.context == 5);
    CHECK(ok({"-n", "face"}).options.force_name);
    CHECK(ok({"--name", "face"}).options.force_name);
    CHECK(ok({"-m", "3", "x"}).options.max_candidates == 3);
    CHECK(ok({"-m3", "x"}).options.max_candidates == 3);
    CHECK(ok({"--max=100000", "x"}).options.max_candidates == 100000);
    CHECK(ok({"--max", "1", "x"}).options.max_candidates == 1);

    // Clusters of short options.
    {
        const auto cl = ok({"-sn", "x"});
        CHECK(cl.options.context == 0 && cl.options.force_name);
        const auto cl2 = ok({"-nC3", "x"});
        CHECK(cl2.options.context == 3 && cl2.options.force_name);
        CHECK(ok({"-nC", "3", "x"}).options.context == 3);
    }

    // Positional arguments keep their order; a lone dash is an argument.
    {
        const auto cl = ok({"a", "-s", "b", "-", "c"});
        CHECK((cl.arguments == Args{"a", "b", "-", "c"}));
        CHECK(cl.options.context == 0);
    }

    // "--" ends options.
    {
        const auto cl = ok({"-s", "--", "-s", "--help", "-C"});
        CHECK((cl.arguments == Args{"-s", "--help", "-C"}));
        CHECK(cl.options.context == 0);
        CHECK(cl.action == options::Action::Lookup);
    }
    CHECK(ok({"--", "--"}).arguments == Args{"--"});

    // Help and version are results.
    CHECK(ok({"-h"}).action == options::Action::Help);
    CHECK(ok({"--help"}).action == options::Action::Help);
    CHECK(ok({"x", "--help"}).action == options::Action::Help);
    CHECK(ok({"-V"}).action == options::Action::Version);
    CHECK(ok({"--version"}).action == options::Action::Version);
    CHECK(ok({"-sV"}).action == options::Action::Version);
    CHECK(ok({"--help", "--bogus"}).action == options::Action::Help);
    CHECK(ok({"--help", "--bogus"}).arguments.empty());

    // Errors.
    CHECK(fails({}));
    CHECK(fails({"-s"}));
    CHECK(fails({"--"}));
    CHECK(fails({"-x", "a"}));
    CHECK(fails({"--bogus", "a"}));
    CHECK(fails({"--cont=2", "a"}));
    CHECK(fails({"-C", "a"}));
    CHECK(fails({"-C", "-1", "a"}));
    CHECK(fails({"-C", "+1", "a"}));
    CHECK(fails({"-C", "", "a"}));
    CHECK(fails({"-C", "1.5", "a"}));
    CHECK(fails({"-C", "1114112", "a"}));
    CHECK(fails({"-C", "99999999999999999999", "a"}));
    CHECK(fails({"a", "-C"}));
    CHECK(fails({"a", "--context"}));
    CHECK(fails({"--context=", "a"}));
    CHECK(fails({"-m", "0", "a"}));
    CHECK(fails({"-m", "100001", "a"}));
    CHECK(fails({"--single=1", "a"}));
    CHECK(fails({"-sx", "a"}));
    CHECK(fails({"-5"}));
    {
        const auto result = options::parse({});
        CHECK(!result.has_value() && result.error().show_usage);
    }
    {
        const auto result = options::parse({"-x", "a"});
        CHECK(!result.has_value() && !result.error().show_usage);
    }

    // Usage text exists and ends in a newline.
    CHECK(!options::usage_text().empty());
    CHECK(options::usage_text().back() == '\n');

    return tellmeutf::test::finish();
}
