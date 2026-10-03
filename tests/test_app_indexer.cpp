#include "nexus/index/app_indexer.hpp"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus::index;

static void write_desktop(const fs::path& dir, const std::string& id, const std::string& name,
                          const std::string& extra = "") {
    fs::create_directories(dir);
    std::ofstream(dir / (id + ".desktop"))
        << "[Desktop Entry]\nType=Application\nName=" << name << "\nExec=" << id << "\n" << extra;
}


static const DesktopEntry* find(const std::vector<DesktopEntry>& apps, const std::string& id) {
    for (const auto& a : apps) if (a.id == id) return &a;
    return nullptr;
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_app_indexer_" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path user = root / "home" / "applications";  // XDG_DATA_HOME/applications
    const fs::path sys = root / "sys" / "applications";    // XDG_DATA_DIRS entry

    // Point every directory the indexer scans at the temp tree.
    const char* old_home = std::getenv("HOME");
    const char* old_xdh = std::getenv("XDG_DATA_HOME");
    const char* old_xdd = std::getenv("XDG_DATA_DIRS");
    const std::string s_home = old_home ? old_home : "", s_xdh = old_xdh ? old_xdh : "", s_xdd = old_xdd ? old_xdd : "";
    setenv("HOME", (root / "fakehome").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "home").c_str(), 1);
    setenv("XDG_DATA_DIRS", (root / "sys").c_str(), 1);

    write_desktop(sys, "plain", "Plain App");
    write_desktop(sys, "nodisplay", "No Display App", "NoDisplay=true\n");
    write_desktop(sys, "hidden", "Hidden App", "Hidden=true\n");
    write_desktop(sys, "dup", "System Dup");
    write_desktop(user, "dup", "User Dup");
    write_desktop(user, "useronly", "User Only");

    // Indexed, no DB: rebuild scans the filesystem
    AppIndexer indexer;
    size_t n = indexer.rebuild_index();
    auto apps = indexer.get_applications();
    assert(n == apps.size());
    // Not asserting an exact count: the indexer also scans hardcoded snap/flatpak dirs the env can't redirect.
    assert(n >= 3);
    assert(find(apps, "plain"));
    assert(find(apps, "useronly"));

    // NoDisplay / Hidden skipped
    assert(!find(apps, "nodisplay"));
    assert(!find(apps, "hidden"));

    // user override wins dedup
    const DesktopEntry* dup = find(apps, "dup");
    assert(dup && dup->name == "User Dup");

    // Removed files drop out of the index
    fs::remove_all(sys);
    fs::remove_all(user);
    indexer.rebuild_index();
    assert(!find(indexer.get_applications(), "plain") && !find(indexer.get_applications(), "dup"));

    // restore env
    old_home ? setenv("HOME", s_home.c_str(), 1) : unsetenv("HOME");
    old_xdh ? setenv("XDG_DATA_HOME", s_xdh.c_str(), 1) : unsetenv("XDG_DATA_HOME");
    old_xdd ? setenv("XDG_DATA_DIRS", s_xdd.c_str(), 1) : unsetenv("XDG_DATA_DIRS");
    fs::remove_all(root);
    return 0;
}
