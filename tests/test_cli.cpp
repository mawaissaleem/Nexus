#include "nexus/app/cli.hpp"
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
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

}  // namespace

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
    for (const char* flag : {"--show", "--hide", "--settings", "config", "-t", "-platform", "-style"})
        assert(!run({flag}, config, aliases).rc);
    // Qt options and session restore: QApplication consumes them
    assert(!run({"-session", "abc"}, config, aliases).rc);
    assert(!run({"--style=fusion"}, config, aliases).rc);
    assert(!run({"-platformtheme", "gtk3"}, config, aliases).rc);

    // --version / -V
    for (const char* flag : {"--version", "-V"}) {
        const Run r = run({flag}, config, aliases);
        assert(r.rc == 0 && r.out == std::string("nexus ") + NEXUS_VERSION + "\n" && r.err.empty());
    }

    // Unknown flag or word: error + usage on stderr, non-zero, never the GUI
    for (const char* bad : {"--bogus", "foo", "no-such-command"}) {
        const Run r = run({bad}, config, aliases);
        assert(r.rc && *r.rc != 0 && r.out.empty());
        assert(has(r.err, std::string("unknown argument: ") + bad) && has(r.err, "Usage:") && has(r.err, "--version"));
    }

    // help
    for (const char* flag : {"--help", "-h"}) {
        const Run r = run({flag}, config, aliases);
        assert(r.rc == 0);
        assert(has(r.out, "Usage:") && has(r.out, "alias add <name> <target>"));
        assert(has(r.out, "Global hotkey: Alt+Space by default"));
    }

    // --version, --help and unknown arguments need no config: run_info_cli with nothing on disk.
    {
        const fs::path empty_cfg = root / "empty_cfg";
        fs::create_directories(empty_cfg);
        setenv("XDG_CONFIG_HOME", empty_cfg.c_str(), 1);
        for (const char* a : {"--version", "-V", "--help", "-h", "--bogus"}) {
            std::ostringstream out, err;
            const auto rc = nexus::app::run_info_cli({a}, out, err);
            assert(rc && (std::string(a) == "--bogus") == (*rc != 0));
        }
        std::ostringstream o, e;
        assert(!nexus::app::run_info_cli({"dir", "list"}, o, e) && !nexus::app::run_info_cli({}, o, e));
        assert(!nexus::app::run_info_cli({"--toggle"}, o, e));
        assert(fs::is_empty(empty_cfg));
        unsetenv("XDG_CONFIG_HOME");
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
    assert(r.rc == 1 && has(r.err, "Failed: there is no alias named \"gh\"") && r.out.empty());
    for (const auto& usage :
         std::vector<std::vector<std::string>>{{"alias"}, {"alias", "add", "x"}, {"alias", "bogus"}}) {
        r = run(usage, config, aliases);
        assert(r.rc == 0 && has(r.out, "Usage: nexus alias"));
    }

    // A save that fails says why on stderr and exits non-zero.
    {
        const std::string blocked = (root / "missing_dir" / "aliases.json").string();
        nexus::core::AliasManager unwritable(blocked);
        r = run({"alias", "add", "x", "https://x.example"}, config, unwritable);
        assert(r.rc == 1 && has(r.err, "Failed: can't write " + blocked) && has(r.err, "No such file or directory"));
        assert(r.out.empty());
    }

    // dir add / list / remove
    const std::string dir1 = (root / "dir1").string();
    r = run({"dir", "add", dir1}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Added search directory: " + dir1));
    r = run({"dir", "list"}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Configured search directories (") && has(r.out, dir1));
    r = run({"dir", "add", (root / "missing").string()}, config, aliases);
    assert(r.rc == 1 && has(r.err, "Failed: can't add ") && has(r.err, "no such directory"));
    r = run({"dir", "remove", dir1}, config, aliases);
    assert(r.rc == 0 && has(r.out, "Removed search directory: " + dir1));
    r = run({"dir", "remove", dir1}, config, aliases);
    assert(r.rc == 1 && has(r.err, "is not in the search directory list"));
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

    // A running launcher is told once and reported; a missing one adds no output.
    int notified = 0;
    std::ostringstream out, err;
    assert(run_cli({"history", "clear"}, config, aliases, out, err, [&] {
               ++notified;
               return true;
           }) == 0);
    assert(notified == 1 && has(out.str(), "Cleared the clipboard history of the running launcher."));
    std::ostringstream out2, err2;
    assert(run_cli({"history", "clear"}, config, aliases, out2, err2, [&] { return false; }) == 0);
    assert(!has(out2.str(), "running launcher") && err2.str().empty());
    // Usage text and failures never notify.
    assert(run_cli({"history"}, config, aliases, out, err,
                   [&] {
                       ++notified;
                       return true;
                   }) == 0 &&
           notified == 1);

    // A database file that isn't SQLite: history clear fails, says why on stderr and exits non-zero.
    std::ofstream(data_home / "nexus" / "nexus.db", std::ios::trunc) << std::string(4096, 'x');
    r = run({"history", "clear"}, config, aliases);
    assert(r.rc == 1 && has(r.err, "Failed: ") && has(r.err, "not a database") && r.out.empty());

    fs::remove_all(root);
    return 0;
}
