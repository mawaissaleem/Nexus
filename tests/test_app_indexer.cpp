#include "nexus/index/app_indexer.hpp"
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <atomic>
#include <string>
#include <thread>
#include <fcntl.h>
#include <sys/resource.h>
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
    auto snap = indexer.get_applications();
    const auto& apps = *snap;
    assert(n == apps.size());
    // Env redirects only some dirs (the indexer also scans hardcoded snap/flatpak ones), so just a lower bound.
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
    assert(!find(*indexer.get_applications(), "plain") && !find(*indexer.get_applications(), "dup"));
    // A snapshot taken before the rebuild is unchanged by it.
    assert(find(apps, "plain") && find(apps, "dup") && apps.size() == n);

    // Searches during a rebuild: concurrent rebuild_index() + get_applications() stay consistent.
    write_desktop(sys, "concurrent", "Concurrent App");
    indexer.rebuild_index();
    std::atomic<bool> done{false};
    std::thread rebuilder([&] {
        for (int i = 0; i < 20; ++i) assert(indexer.rebuild_index() >= 1);
        done = true;
    });
    while (!done) assert(find(*indexer.get_applications(), "concurrent"));
    rebuilder.join();

    // Explicit directory list (seam): exactly these dirs, hermetic regardless of host snap/flatpak.
    {
        write_desktop(sys, "plain", "Plain App");
        write_desktop(user, "dup", "User Dup");
        write_desktop(sys, "dup", "System Dup");
        AppIndexer exact(nullptr, std::vector<fs::path>{user, sys});
        assert(exact.rebuild_index() == 3);  // plain, dup, concurrent
        assert(find(*exact.get_applications(), "dup")->name == "User Dup");
        AppIndexer none(nullptr, std::vector<fs::path>{root / "missing"});
        assert(none.rebuild_index() == 0);
    }

    // Startup rescan: initialize() serves the stale cache; rebuild_index() (what main runs on its startup worker
    // thread) finds an app added while Nexus was off.
    {
        const fs::path dir = root / "rescan";
        write_desktop(dir, "old", "Old App");
        auto db = std::make_shared<Database>((root / "rescan.db").string());
        assert(db->open());
        {
            AppIndexer first(db, std::vector<fs::path>{dir});
            assert(first.rebuild_index() == 1);  // cache holds only "old"
        }
        write_desktop(dir, "fresh", "Fresh App");  // installed while "off"
        AppIndexer second(db, std::vector<fs::path>{dir});
        second.initialize();
        assert(!find(*second.get_applications(), "fresh"));  // cache-only first paint
        assert(second.rebuild_index() == 2);
        assert(find(*second.get_applications(), "fresh"));
    }

    // Filesystem errors never throw out of rebuild_index; apps in other dirs are still indexed.
    {
        const fs::path good = root / "fs_good", bad = root / "fs_bad";
        write_desktop(good, "goodapp", "Good App");
        write_desktop(bad / "locked", "lockedapp", "Locked App");
        write_desktop(bad, "badtop", "Bad Top");
        fs::permissions(bad / "locked", fs::perms::none);  // ignored when running as root: then it just scans fully
        std::ofstream(root / "notadir") << "x";
        AppIndexer err(nullptr, std::vector<fs::path>{root / "notadir", bad, root / "vanished", good});
        bool threw = false;
        try { err.rebuild_index(); } catch (...) { threw = true; }
        fs::permissions(bad / "locked", fs::perms::owner_all);  // restore before asserts so cleanup works
        assert(!threw);
        assert(find(*err.get_applications(), "goodapp") && find(*err.get_applications(), "badtop"));
    }

    // Regression (D-76): an error inside operator++ (EMFILE opening a deep subdir; the iterator holds
    // one fd per level) used to throw out of the range-for. Now that dir stops and the next is scanned.
    {
        fs::path deep = root / "fs_deep";
        for (int i = 0; i < 30; ++i) deep /= "s";
        fs::create_directories(deep);
        const fs::path after = root / "fs_after";
        write_desktop(after, "afterapp", "After App");
        AppIndexer err(nullptr, std::vector<fs::path>{root / "fs_deep", after});

        rlimit old{};
        assert(getrlimit(RLIMIT_NOFILE, &old) == 0);
        const int probe = open("/dev/null", O_RDONLY);  // lowest free fd
        assert(probe >= 0);
        close(probe);
        rlimit low = old;
        low.rlim_cur = static_cast<rlim_t>(probe) + 8;  // rlim_max untouched so restoring needs no privilege
        assert(setrlimit(RLIMIT_NOFILE, &low) == 0);
        bool threw = false;
        try { err.rebuild_index(); } catch (...) { threw = true; }
        assert(setrlimit(RLIMIT_NOFILE, &old) == 0);  // restore before any assert can abort
        assert(!threw);
        assert(find(*err.get_applications(), "afterapp"));
    }

    // D-54: a higher-priority hidden/no-display override masks the system copy; valid override replaces it;
    // a broken override does not mask.
    {
        const fs::path usr = root / "mask_user", sys = root / "mask_sys";
        for (const char* id : {"hid", "nod", "val", "brk", "keep"}) write_desktop(sys, id, std::string("Sys ") + id);
        write_desktop(usr, "hid", "U", "Hidden=true\n");
        write_desktop(usr, "nod", "U", "NoDisplay=true\n");
        write_desktop(usr, "val", "User Val");
        fs::create_directories(usr);
        std::ofstream(usr / "brk.desktop") << "[Desktop Entry]\nType=Link\n";
        AppIndexer m(nullptr, std::vector<fs::path>{usr, sys});
        assert(m.rebuild_index() == 3);
        const auto& apps = *m.get_applications();
        assert(!find(apps, "hid") && !find(apps, "nod"));
        assert(find(apps, "val") && find(apps, "val")->name == "User Val");
        assert(find(apps, "brk") && find(apps, "brk")->name == "Sys brk");
        assert(find(apps, "keep"));
        // bare Hidden=true file with no Name/Exec also masks
        std::ofstream(usr / "keep.desktop") << "[Desktop Entry]\nHidden=true\n";
        m.rebuild_index();
        assert(!find(*m.get_applications(), "keep"));
    }

    // restore env
    old_home ? setenv("HOME", s_home.c_str(), 1) : unsetenv("HOME");
    old_xdh ? setenv("XDG_DATA_HOME", s_xdh.c_str(), 1) : unsetenv("XDG_DATA_HOME");
    old_xdd ? setenv("XDG_DATA_DIRS", s_xdd.c_str(), 1) : unsetenv("XDG_DATA_DIRS");
    fs::remove_all(root);
    return 0;
}
