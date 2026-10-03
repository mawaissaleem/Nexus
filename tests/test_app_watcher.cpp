#include "nexus/index/app_indexer.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <fstream>
#include <functional>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus::index;

namespace {

void write_desktop(const fs::path& dir, const std::string& file, const std::string& name) {
    fs::create_directories(dir);
    std::ofstream(dir / file) << "[Desktop Entry]\nType=Application\nName=" << name << "\nExec=x\n";
}

// Bounded polling (10 ms steps, up to 3 s) instead of fixed sleeps.
bool wait_until(const std::function<bool()>& pred) {
    for (int i = 0; i < 300; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

std::string name_of(const AppIndexer& ix, const std::string& id) {
    const auto snap = ix.get_applications();  // keep the owner alive: in C++20 a temporary in the range-init dies first
    for (const auto& a : *snap) if (a.id == id) return a.name;
    return "";
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_test_app_watcher_" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path user = root / "user";  // does not exist yet at start: only the system dir is watched
    const fs::path sys = root / "sys";
    write_desktop(sys, "base.desktop", "Base");
    fs::create_directories(sys / "kde");

    auto db = std::make_shared<Database>((root / "t.db").string());
    assert(db->open());
    AppIndexer ix(db, std::vector<fs::path>{user, sys});
    ix.rebuild_index();
    assert(name_of(ix, "base") == "Base");
    assert(ix.start_watching());

    // add
    write_desktop(sys, "added.desktop", "Added");
    assert(wait_until([&] { return name_of(ix, "added") == "Added"; }));

    // installer style: write a temp file, rename into place; the temp name alone must not matter
    write_desktop(sys, "tmp123", "Renamed");
    fs::rename(sys / "tmp123", sys / "renamed.desktop");
    assert(wait_until([&] { return name_of(ix, "renamed") == "Renamed"; }));

    // overwrite via rename replaces the content
    write_desktop(sys, "tmp456", "Renamed v2");
    fs::rename(sys / "tmp456", sys / "renamed.desktop");
    assert(wait_until([&] { return name_of(ix, "renamed") == "Renamed v2"; }));

    // subdirectory (id kde-foo), including a directory created after start
    write_desktop(sys / "kde", "foo.desktop", "Kde Foo");
    assert(wait_until([&] { return name_of(ix, "foo") == "Kde Foo"; }));
    write_desktop(sys / "newsub", "bar.desktop", "Bar");
    assert(wait_until([&] { return name_of(ix, "bar") == "Bar"; }));
    write_desktop(sys / "newsub", "baz.desktop", "Baz");  // newsub is watched now
    assert(wait_until([&] { return name_of(ix, "baz") == "Baz"; }));

    // remove: gone from memory and from the DB cache a restart would load
    fs::remove(sys / "added.desktop");
    assert(wait_until([&] { return name_of(ix, "added").empty(); }));
    assert(wait_until([&] {
        for (const auto& a : db->load_applications()) if (a.id == "added") return false;
        return true;
    }));

    // a non-.desktop file does not rebuild: a snapshot taken before is still the live one
    const auto before = ix.get_applications();
    std::ofstream(sys / "notes.txt") << "x";
    std::ofstream(sys / ".base.desktop.swp") << "x";
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // long past the 50 ms debounce
    assert(ix.get_applications() == before);

    // user dir appearing later is not watched (documented limit) but a rebuild picks it up and priority holds
    write_desktop(user, "base.desktop", "User Base");
    write_desktop(sys, "poke.desktop", "Poke");  // triggers a rebuild that now sees the user dir
    assert(wait_until([&] { return name_of(ix, "base") == "User Base"; }));

    // stop() while rebuilds are running: no crash, no hang
    std::atomic<bool> go{true};
    std::thread churn([&] {
        for (int i = 0; go && i < 200; ++i) write_desktop(sys, "churn" + std::to_string(i % 5) + ".desktop", "C");
    });
    std::this_thread::sleep_for(std::chrono::milliseconds(60));
    ix.stop_watching();
    go = false;
    churn.join();
    ix.stop_watching();  // twice is fine

    // restartable
    assert(ix.start_watching());
    write_desktop(sys, "again.desktop", "Again");
    assert(wait_until([&] { return name_of(ix, "again") == "Again"; }));

    ix.stop_watching();

    // an app dir that is itself a symlink (~/.local/share/applications -> ~/dotfiles/...) is watched via its target
    const fs::path real = root / "dotfiles" / "applications";
    const fs::path link = root / "linked";
    fs::create_directories(real / "sub");
    fs::create_directory_symlink(real, link);
    {
        AppIndexer linked(nullptr, std::vector<fs::path>{link});
        linked.rebuild_index();
        assert(linked.start_watching());
        write_desktop(real, "dot.desktop", "Dot");
        assert(wait_until([&] { return name_of(linked, "dot") == "Dot"; }));
        write_desktop(real / "sub", "deep.desktop", "Deep");
        assert(wait_until([&] { return name_of(linked, "deep") == "Deep"; }));
    }
    // link and target both configured: the same inotify watch twice is harmless
    {
        AppIndexer both(nullptr, std::vector<fs::path>{link, real});
        both.rebuild_index();
        assert(both.start_watching());
        write_desktop(link, "twice.desktop", "Twice");
        assert(wait_until([&] { return name_of(both, "twice") == "Twice"; }));
        fs::remove(real / "twice.desktop");
        assert(wait_until([&] { return name_of(both, "twice").empty(); }));
    }

    fs::remove_all(root);
    return 0;
}
