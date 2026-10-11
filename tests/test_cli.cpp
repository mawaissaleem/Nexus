#include "nexus/app/cli.hpp"
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
using nexus::app::run_cli;

namespace {

struct Run {
    std::optional<int> rc;
    std::string out, err;
};

Run run(const std::vector<std::string>& args, nexus::core::ConfigManager& config, nexus::core::AliasManager& aliases) {
    std::ostringstream out, err;
    Run r;
    r.rc = run_cli(args, config, aliases, out, err);
    r.out = out.str();
    r.err = err.str();
    return r;
}

bool has(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_test_cli_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root / "dir1");
    // The database lives under XDG_DATA_HOME; point it at the temp tree so nothing real is touched.
    const fs::path data_home = root / "data";
    setenv("XDG_DATA_HOME", data_home.c_str(), 1);

    nexus::core::ConfigManager config((root / "config.json").string());
    nexus::core::AliasManager aliases((root / "aliases.json").string());

    // Not CLI commands: caller starts the GUI.
    assert(!run({}, config, aliases).rc);
    assert(!run({"--toggle"}, config, aliases).rc);
    assert(!run({"settings"}, config, aliases).rc);
    assert(!run({"no-such-command"}, config, aliases).rc);

    // help
    for (const char* flag : {"--help", "-h"}) {
        const Run r = run({flag}, config, aliases);
        assert(r.rc == 0);
        assert(has(r.out, "Usage:") && has(r.out, "alias add <name> <target>"));
        assert(has(r.out, "Global hotkey: " + config.get_shortcut()));
    }

    // alias add / list / remove, including multi-word and '>' targets
    Run r = run({"alias", "add", "gh", "https://github.com"}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Saved alias: 'gh' -> https://github.com"));
    r = run({"alias", "add", "up", ">sudo", "apt", "update"}, config, aliases);
    assert(r.rc == 0 && has(r.out, "-> >sudo apt update"));
    assert(aliases.get_aliases().size() >= 2);
    r = run({"alias", "list"}, config, aliases);
    assert(r.rc == 0 && has(r.out, " - gh -> https://github.com") && has(r.out, " - up -> >sudo apt update"));
    r = run({"alias", "remove", "gh"}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Removed alias: gh"));
    r = run({"alias", "remove", "gh"}, config, aliases);
    assert(r.rc == 1 && has(r.err, "Alias not found: gh") && r.out.empty());
    for (const auto& usage : std::vector<std::vector<std::string>>{{"alias"}, {"alias", "add", "x"}, {"alias", "bogus"}}) {
        r = run(usage, config, aliases);
        assert(r.rc == 0 && has(r.out, "Usage: nexus alias"));
    }

    // dir add / list / remove
    const std::string dir1 = (root / "dir1").string();
    r = run({"dir", "add", dir1}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Added search directory: " + dir1));
    r = run({"dir", "list"}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Configured search directories (") && has(r.out, dir1));
    r = run({"dir", "add", (root / "missing").string()}, config, aliases);
    assert(r.rc == 1 && has(r.err, "directory does not exist"));
    r = run({"dir", "remove", dir1}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Removed search directory: " + dir1));
    r = run({"dir", "remove", dir1}, config, aliases);
    assert(r.rc == 1 && has(r.err, "Directory not found in search list"));
    for (const auto& usage : std::vector<std::vector<std::string>>{{"dir"}, {"dir", "add"}, {"dir", "bogus"}}) {
        r = run(usage, config, aliases);
        assert(r.rc == 0 && has(r.out, "Usage: nexus dir"));
    }

    // query without a term fails before any indexer is built
    r = run({"query"}, config, aliases);
    assert(r.rc == 1 && has(r.err, "query requires a search term"));
    r = run({"-q"}, config, aliases);
    assert(r.rc == 1);

    // None of the above opened the database or scanned applications.
    assert(!fs::exists(data_home));

    // history usage text does not open it either
    r = run({"history"}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Usage: nexus history clear"));
    assert(!fs::exists(data_home));

    // history clear opens the database (created in the temp data dir) and reports counts.
    r = run({"history", "clear"}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Cleared 0 usage events and 0 query history entries."));
    assert(fs::exists(data_home / "nexus" / "nexus.db"));

    fs::remove_all(root);
    return 0;
}
