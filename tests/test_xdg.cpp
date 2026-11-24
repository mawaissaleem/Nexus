// `~` expansion: needs an absolute HOME, never falls back to a path relative to the cwd.
#include "check.hpp"
#include "nexus/utils/xdg.hpp"
#include <cstdlib>
#include <optional>
#include <string>

using nexus::utils::expand_home;

static std::string shown(const std::optional<std::string>& value) { return value ? *value : "<none>"; }

int main() {
    const char* old_home = std::getenv("HOME");
    const std::string saved_home = old_home ? old_home : "";

    setenv("HOME", "/home/someone", 1);
    CHECK_EQ(shown(expand_home("~")), std::string("/home/someone"));
    CHECK_EQ(shown(expand_home("~/docs")), std::string("/home/someone/docs"));
    CHECK_EQ(shown(expand_home("/abs/path")), std::string("/abs/path"));
    CHECK_EQ(shown(expand_home("rel/path")), std::string("rel/path"));
    CHECK_EQ(shown(expand_home("")), std::string(""));
    // Only `~` and `~/...` expand; `~name` is another user's home and stays as written.
    CHECK_EQ(shown(expand_home("~name/x")), std::string("~name/x"));

    // A relative or missing HOME can't anchor `~`.
    setenv("HOME", "relative/home", 1);
    CHECK_EQ(shown(expand_home("~/x")), std::string("<none>"));
    setenv("HOME", ".", 1);
    CHECK_EQ(shown(expand_home("~")), std::string("<none>"));
    unsetenv("HOME");
    CHECK_EQ(shown(expand_home("~/x")), std::string("<none>"));
    // Paths that need no home are untouched even then.
    CHECK_EQ(shown(expand_home("/tmp")), std::string("/tmp"));

    if (old_home) {
        setenv("HOME", saved_home.c_str(), 1);
    }
    return 0;
}
