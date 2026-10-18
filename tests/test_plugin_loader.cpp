// Plugin discovery, registration and shutdown, against temp plugin dirs (never the real ones).
#include "nexus/app/services.hpp"
#include "nexus/providers/plugin_loader.hpp"

#include <cassert>
#include <chrono>
#include <cstdlib>
#include <csignal>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>

using namespace nexus;
using namespace std::chrono_literals;
namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static fs::path root;

static void write_plugin(const fs::path& base, const std::string& id, const std::string& dir_name = "", const std::string& body = "") {
    const fs::path dir = base / (dir_name.empty() ? id : dir_name);
    fs::create_directories(dir);
    std::ofstream(dir / "plugin.json") << "{\"id\":\"" << id << "\",\"api_version\":1,\"exec\":\"run.sh\",\"prefixes\":[\"\"]}";
    std::ofstream(dir / "run.sh") << (body.empty() ? "#!/bin/sh\necho $$ > pid\necho '{\"type\":\"hello\",\"api_version\":1}'\nwhile read -r l; do :; done\n" : body);
    fs::permissions(dir / "run.sh", fs::perms::owner_all);
}

static std::set<std::string> ids(const std::vector<providers::plugin::Manifest>& ms) {
    std::set<std::string> r;
    for (const auto& m : ms) r.insert(m.id);
    return r;
}

static void test_discover() {
    const fs::path user = root / "d1" / "user", sys = root / "d1" / "sys";
    write_plugin(user, "good");
    write_plugin(user, "shared");
    write_plugin(sys, "shared");
    write_plugin(sys, "sysonly");
    write_plugin(user, "settings");     // reserved
    write_plugin(user, "files");        // built-in id
    write_plugin(user, "mismatch", "otherdir");  // invalid: id != dir name
    fs::create_directories(user / "empty");        // invalid: no plugin.json
    std::ofstream(user / "stray.txt") << "x";
    const auto found = providers::discover_plugins({user, sys, root / "d1" / "missing"}, {"settings", "aliases", "files"});
    assert((ids(found) == std::set<std::string>{"good", "shared", "sysonly"}));
    for (const auto& m : found) {
        if (m.id == "shared") assert(m.dir == user / "shared");  // user dir wins
    }
}

static void test_search_dirs() {
    setenv("XDG_DATA_HOME", "/h/data", 1);
    setenv("XDG_DATA_DIRS", "/a:/b", 1);
    auto d = providers::plugin_search_dirs();
    assert((d == std::vector<fs::path>{"/h/data/nexus/plugins", "/a/nexus/plugins", "/b/nexus/plugins"}));
    unsetenv("XDG_DATA_DIRS");
    d = providers::plugin_search_dirs();
    assert(d.size() == 3 && d[1] == "/usr/local/share/nexus/plugins" && d[2] == "/usr/share/nexus/plugins");
    const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";
    unsetenv("XDG_DATA_HOME");
    setenv("HOME", "/hh", 1);
    d = providers::plugin_search_dirs();
    assert(d[0] == "/hh/.local/share/nexus/plugins");
    setenv("HOME", home.c_str(), 1);
    // The services test opens a database under $XDG_DATA_HOME: keep it inside the temp dir.
    setenv("XDG_DATA_HOME", (root / "data").c_str(), 1);
}

static bool gone(int pid) { return kill(pid, 0) != 0; }
static int read_pid(const fs::path& dir) { int p = 0; std::ifstream(dir / "pid") >> p; return p; }

static void test_services_and_shutdown() {
    const fs::path base = root / "d2";
    write_plugin(base, "stubborn");  // ignores `shutdown`, exits when stdin closes
    write_plugin(base, "off");
    write_plugin(base, "applications");  // reserved: must not replace the built-in
    // Ignores both `shutdown` and EOF, so only SIGKILL ends it.
    write_plugin(base, "deaf", "", "#!/bin/sh\necho $$ > pid\necho '{\"type\":\"hello\",\"api_version\":1}'\ntrap '' TERM HUP\nwhile :; do sleep 1; done\n");

    core::ConfigManager config((root / "config.json").string());
    core::AliasManager aliases((root / "aliases.json").string());
    config.set_provider_enabled("off", false);
    auto s = app::make_services(config, aliases, {base});
    assert(s.plugins.size() == 3);  // stubborn, off, deaf
    assert(!fs::exists(base / "stubborn" / "pid"));  // lazy: nothing spawned by registration

    // a disabled plugin is never spawned; enabled ones start on the first search
    (void)s.engine->search(core::Query("hello"));
    assert(fs::exists(base / "stubborn" / "pid") && fs::exists(base / "deaf" / "pid"));
    assert(!fs::exists(base / "off" / "pid"));
    const int deaf = read_pid(base / "deaf"), stubborn = read_pid(base / "stubborn");

    const auto t0 = Clock::now();
    app::shutdown_services(s);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    assert(ms < 600);
    assert(gone(deaf) && gone(stubborn));  // reaped, not zombies
    (void)s.engine->search(core::Query("again"));
    assert(read_pid(base / "stubborn") == stubborn && gone(stubborn));  // no respawn after shutdown
    app::shutdown_services(s);                               // twice is fine
}

int main() {
    root = fs::temp_directory_path() / ("nexus_plugin_loader_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);
    test_discover();
    test_search_dirs();
    test_services_and_shutdown();
    fs::remove_all(root);
    std::cout << "test_plugin_loader OK\n";
    return 0;
}
