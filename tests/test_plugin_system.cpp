// The shipped system plugin (plugins/system), run for real through PluginProvider from a temp copy.
// Only the decoded results are inspected: none of the commands is ever executed.
#include "nexus/providers/plugin_provider.hpp"

#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

using namespace nexus;
namespace fs = std::filesystem;

static const std::atomic<bool> no_cancel{false};

static std::vector<core::SearchResult> search(const fs::path& dir, const std::string& raw) {
    auto manifest = providers::plugin::parse_manifest(dir);
    assert(manifest);
    providers::PluginProvider provider(*manifest, {std::chrono::milliseconds(3000)});
    assert(provider.prefixes() == std::vector<std::string>{""});
    return provider.search(core::Query(raw, ""), no_cancel);
}

static const core::SearchResult* find(const std::vector<core::SearchResult>& r, const std::string& id) {
    for (const auto& x : r) if (x.id == id) return &x;
    return nullptr;
}

static void check(const fs::path& dir, const std::string& query, const std::string& id,
                  const std::vector<std::string>& argv, bool confirm) {
    auto r = search(dir, query);
    assert(r.size() == 1 && r[0].id == id);
    assert(r[0].action_kind == core::ActionKind::Launch && !r[0].terminal);
    assert(r[0].execution_payload == providers::plugin::argv_to_exec_line(argv) && r[0].confirm == confirm);
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_system_test_" + std::to_string(getpid()));
    const fs::path dir = root / "system";
    fs::create_directories(dir);
    for (const char* f : {"plugin.json", "system.py"})
        fs::copy_file(fs::path(NEXUS_SOURCE_DIR) / "plugins/system" / f, dir / f);

    setenv("XDG_SESSION_ID", "c7", 1);
    check(dir, "lock", "lock", {"loginctl", "lock-session"}, false);
    check(dir, "logout", "logout", {"loginctl", "terminate-session", "c7"}, true);
    check(dir, "sleep", "sleep", {"systemctl", "suspend"}, false);
    check(dir, "restart", "restart", {"systemctl", "reboot"}, true);
    check(dir, "shutdown", "shutdown", {"systemctl", "poweroff"}, true);
    check(dir, "SHUT", "shutdown", {"systemctl", "poweroff"}, true);  // case-insensitive prefix match

    assert(search(dir, "l").empty());        // one letter: too noisy
    assert(search(dir, "firefox").empty());  // unrelated queries get nothing

    unsetenv("XDG_SESSION_ID");  // no session id: logout is omitted, the rest stay
    assert(search(dir, "logout").empty());
    assert(!find(search(dir, "lo"), "logout") && find(search(dir, "lo"), "lock"));

    fs::remove_all(root);
}
