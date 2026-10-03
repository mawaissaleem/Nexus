#include "nexus/core/config_manager.hpp"
#include "nexus/index/database.hpp"
#include "nexus/index/file_indexer.hpp"
#include "nexus/index/file_watcher.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus;

namespace {

const std::atomic<bool> kNoCancel{false};

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

// Bounded polling (10 ms steps, up to 3 s) instead of fixed sleeps.
bool wait_until(const std::function<bool()>& pred) {
    for (int i = 0; i < 300; ++i) {
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return pred();
}

bool indexed(index::Database& db, const fs::path& p) {
    for (const auto& e : db.find_files(p.filename().string())) if (e.path == p.string()) return true;
    return false;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_test_file_watcher_" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path tree = root / "tree";
    const fs::path outside = root / "outside";
    touch(tree / "docs" / "old.txt");
    touch(tree / "a" / "b" / "c" / "shallow.txt");   // level 3 dir c: still in scope (max_depth 3)
    touch(tree / "node_modules" / "pkg.js");
    fs::create_directories(outside);

    auto db = std::make_shared<index::Database>((root / "t.db").string());
    assert(db->open());
    index::FileIndexer indexer(db);
    index::FileIndexOptions opts{{tree.string()}, core::ConfigManager::default_file_exclusions(), 3};

    std::atomic<int> overflows{0};
    {
        index::FileWatcher watcher(db, [&]() { ++overflows; });
        watcher.arm(opts, kNoCancel);
        // tree, docs, a, a/b, a/b/c are watched; node_modules is excluded.
        assert(watcher.watch_count() == 5);

        // Protocol: events that happen while the rebuild runs are held, then replayed against the disk.
        touch(tree / "docs" / "during.txt");      // seen by the rebuild; deleted again before resume
        indexer.rebuild(opts, kNoCancel);
        assert(indexed(*db, tree / "docs" / "during.txt"));
        fs::remove(tree / "docs" / "during.txt");
        touch(tree / "docs" / "late.txt");        // created after the walk: only the held event knows
        assert(!indexed(*db, tree / "docs" / "late.txt"));
        watcher.resume();
        assert(wait_until([&] { return indexed(*db, tree / "docs" / "late.txt"); }));
        assert(wait_until([&] { return !indexed(*db, tree / "docs" / "during.txt"); }));  // no resurrection

        // create / delete
        touch(tree / "docs" / "report.txt");
        assert(wait_until([&] { return indexed(*db, tree / "docs" / "report.txt"); }));
        fs::remove(tree / "docs" / "report.txt");
        assert(wait_until([&] { return !indexed(*db, tree / "docs" / "report.txt"); }));

        // rename
        fs::rename(tree / "docs" / "old.txt", tree / "docs" / "renamed.txt");
        assert(wait_until([&] { return indexed(*db, tree / "docs" / "renamed.txt") && !indexed(*db, tree / "docs" / "old.txt"); }));

        // new subdirectory with a file inside, created at once from outside (moved in): contents are indexed too
        touch(outside / "incoming" / "inner" / "payload.txt");
        fs::rename(outside / "incoming", tree / "incoming");
        assert(wait_until([&] { return indexed(*db, tree / "incoming" / "inner" / "payload.txt"); }));
        // ...and it is watched now
        touch(tree / "incoming" / "inner" / "later.txt");
        assert(wait_until([&] { return indexed(*db, tree / "incoming" / "inner" / "later.txt"); }));

        // plain mkdir, then a file in it
        fs::create_directories(tree / "fresh");
        touch(tree / "fresh" / "inside.txt");
        assert(wait_until([&] { return indexed(*db, tree / "fresh" / "inside.txt"); }));

        // rename of a directory within scope moves its rows and its watches
        fs::rename(tree / "fresh", tree / "fresher");
        assert(wait_until([&] { return indexed(*db, tree / "fresher" / "inside.txt") && !indexed(*db, tree / "fresh" / "inside.txt"); }));
        touch(tree / "fresher" / "after_rename.txt");
        assert(wait_until([&] { return indexed(*db, tree / "fresher" / "after_rename.txt"); }));

        // directory moved out of scope: its rows are gone, later changes there are not tracked
        fs::rename(tree / "incoming", outside / "gone");
        assert(wait_until([&] { return !indexed(*db, tree / "incoming" / "inner" / "payload.txt") && !indexed(*db, tree / "incoming"); }));
        touch(outside / "gone" / "inner" / "ghost.txt");
        touch(tree / "docs" / "sentinel_gone.txt");
        assert(wait_until([&] { return indexed(*db, tree / "docs" / "sentinel_gone.txt"); }));
        assert(!indexed(*db, outside / "gone" / "inner" / "ghost.txt"));
        assert(!indexed(*db, tree / "incoming" / "inner" / "ghost.txt"));

        // depth rule: a directory created at the depth limit is listed but not entered
        touch(tree / "a" / "b" / "c" / "d" / "deep.txt");
        touch(tree / "a" / "b" / "c" / "sentinel_depth.txt");
        assert(wait_until([&] { return indexed(*db, tree / "a" / "b" / "c" / "sentinel_depth.txt"); }));
        assert(indexed(*db, tree / "a" / "b" / "c" / "d"));
        assert(!indexed(*db, tree / "a" / "b" / "c" / "d" / "deep.txt"));

        // excluded and hidden names are ignored; the sentinel proves the batch was processed
        touch(tree / "node_modules" / "more.js");
        fs::create_directories(tree / "build");
        touch(tree / "build" / "o.o");
        touch(tree / "docs" / ".hidden_file");
        touch(tree / ".hidden_dir" / "s.txt");
        touch(tree / "docs" / "sentinel_ignored.txt");
        assert(wait_until([&] { return indexed(*db, tree / "docs" / "sentinel_ignored.txt"); }));
        assert(!indexed(*db, tree / "node_modules" / "more.js"));
        assert(!indexed(*db, tree / "build"));
        assert(!indexed(*db, tree / "docs" / ".hidden_file"));
        assert(!indexed(*db, tree / ".hidden_dir"));
        assert(!indexed(*db, tree / ".hidden_dir" / "s.txt"));

        // a rebuild re-arms with a new scope: only the new directory is watched
        opts.directories = {(tree / "docs").string()};
        watcher.arm(opts, kNoCancel);
        indexer.rebuild(opts, kNoCancel);
        watcher.resume();
        assert(watcher.watch_count() == 1);
        touch(tree / "docs" / "newscope.txt");
        assert(wait_until([&] { return indexed(*db, tree / "docs" / "newscope.txt"); }));

        // stop() returns promptly on an idle watcher
        const auto t0 = std::chrono::steady_clock::now();
        watcher.stop();
        assert(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500));
        assert(overflows == 0);
    }

    // Watch cap: logs once, keeps the watches it has, index still served, no crash.
    {
        opts.directories = {tree.string()};
        indexer.rebuild(opts, kNoCancel);
        index::FileWatcher capped(db, [] {}, /*max_watches=*/2);
        capped.arm(opts, kNoCancel);
        assert(capped.limit_reached());
        assert(capped.watch_count() == 2);
        capped.resume();
        assert(indexed(*db, tree / "docs" / "renamed.txt"));
        touch(tree / "docs" / "newscope2.txt");  // may or may not be watched; must not crash either way
    }

    // A root with a trailing slash: watcher paths must match the rebuild's (`<root>/x`, not `<root>//x`).
    {
        const fs::path slash = root / "slash";
        touch(slash / "a.txt");
        const index::FileIndexOptions sopts{{slash.string() + "/"}, {}, 3};
        index::FileWatcher watcher(db, [] {});
        watcher.arm(sopts, kNoCancel);
        indexer.rebuild(sopts, kNoCancel);
        watcher.resume();
        assert(indexed(*db, slash / "a.txt"));
        fs::remove(slash / "a.txt");
        touch(slash / "b.txt");
        assert(wait_until([&] { return indexed(*db, slash / "b.txt") && !indexed(*db, slash / "a.txt"); }));
        assert(!indexed(*db, slash.string() + "//b.txt"));
    }

    // stop() while handle() is busy with a large moved-in tree: no crash (stop joins before releasing), and returns
    // promptly (the walk is cancelled).
    {
        const fs::path big = outside / "big";
        for (int i = 0; i < 15; ++i)
            for (int j = 0; j < 15; ++j)
                for (int k = 0; k < 4; ++k) touch(big / std::to_string(i) / std::to_string(j) / (std::to_string(k) + ".txt"));
        const index::FileIndexOptions bopts{{tree.string()}, {}, 3};
        for (const int delay_ms : {0, 20, 52, 80}) {
            index::FileWatcher watcher(db, [] {});
            watcher.arm(bopts, kNoCancel);
            watcher.resume();
            fs::rename(big, tree / "big");
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
            const auto t0 = std::chrono::steady_clock::now();
            watcher.stop();
            assert(std::chrono::steady_clock::now() - t0 < std::chrono::milliseconds(500));
            fs::rename(tree / "big", big);
        }
    }

    // Database::apply_file_changes: subtree delete is exact (siblings sharing a prefix survive), is_root is kept.
    {
        auto db2 = std::make_shared<index::Database>((root / "t2.db").string());
        assert(db2->open());
        assert(db2->replace_files({{"/r", "r", true, true}, {"/r/a", "a", true, false}, {"/r/a/x.txt", "x.txt", false, false},
                                   {"/r/a b", "a b", true, false}, {"/r/a0", "a0", true, false}, {"/r/a%", "a%", true, false}}));
        assert(db2->apply_file_changes({"/r/a"}, {{"/r", "r", true, false}, {"/r/new", "new", false, false}}));
        auto has = [&](const std::string& pattern, const std::string& path) {
            for (const auto& e : db2->find_files(pattern)) if (e.path == path) return true;
            return false;
        };
        assert(!has("a", "/r/a") && !has("x.txt", "/r/a/x.txt"));
        assert(has("a b", "/r/a b") && has("a0", "/r/a0") && has("a%", "/r/a%"));
        assert(has("new", "/r/new"));
        bool root_kept = false;
        for (const auto& e : db2->find_files("r")) if (e.path == "/r" && e.is_root) root_kept = true;
        assert(root_kept);
    }

    fs::remove_all(root);
    std::cout << "test_file_watcher: OK\n";
    return 0;
}
