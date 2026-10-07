#include "nexus/index/database.hpp"
#include <ctime>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sqlite3.h>
#include <unistd.h>

int main() {
    using namespace nexus::index;

    Database db(":memory:");
    assert(db.open());

    // Save applications
    DesktopEntry app1;
    app1.id = "app1";
    app1.name = "App One";
    app1.exec = "app1 %u";
    app1.categories = {"Utility"};

    DesktopEntry app2;
    app2.id = "app2";
    app2.name = "App Two";
    app2.exec = "app2";
    app2.categories = {"Development"};

    bool saved = db.save_applications({app1, app2});
    assert(saved);

    auto loaded = db.load_applications();
    assert(loaded.size() == 2);

    // Test usage tracking
    assert(db.get_usage_counts("app").empty());
    assert(db.get_usage_count("app1") == 0);
    assert(db.record_usage("app1", "app"));
    assert(db.record_usage("app1", "one"));
    assert(db.get_usage_count("app1") == 2);
    assert(db.get_usage_count("app2") == 0);

    // Bulk counts: one entry per used app, unused apps absent
    auto counts = db.get_usage_counts("app");
    assert(counts.size() == 1 && counts.at("app1").count == 1);   // "app" matches "app"; "one" does not
    assert(db.get_usage_counts("one").at("app1").count == 1);
    assert(db.get_usage_counts("zzz").empty());

    // Query-conditioned: past "co" boosts "co"/"cod"/"c", not "fi"; case/space-insensitive; empty past query ignored
    assert(db.record_usage("appX", "  Co "));
    assert(db.record_usage("appY", ""));
    assert(db.get_usage_counts("co").at("appX").count == 1);
    assert(db.get_usage_counts("cod").at("appX").count == 1);
    assert(db.get_usage_counts("c").at("appX").count == 1);
    assert(db.get_usage_counts("fi").count("appX") == 0);
    assert(db.get_usage_counts("co").count("appY") == 0);
    // Tabs/newlines are trimmed like Query::normalized() does (SQL trim() would keep them)
    assert(db.record_usage("appT", "\tTe\n"));
    assert(db.get_usage_counts("te").at("appT").count == 1);

    // last_used is the newest matching event's time, in Unix seconds (a fresh event is "now")
    {
        const auto now = static_cast<std::int64_t>(std::time(nullptr));
        const auto last = db.get_usage_counts("te").at("appT").last_used;
        assert(last >= now - 5 && last <= now + 5);
    }

    // Test query history
    assert(db.record_query("app", "app1", "applications"));

    db.close();

    // Unwritable primary path must not create ./.nexus.db or /tmp/nexus.db (D-43).
    namespace fs = std::filesystem;
    fs::path dir = fs::temp_directory_path() / ("nexus_db_test_" + std::to_string(::getpid()));
    fs::create_directories(dir);
    std::ofstream(dir / "blocker") << "x";  // a file used as parent dir -> open fails
    fs::path old_cwd = fs::current_path();
    fs::current_path(dir);
    std::error_code ec;
    bool tmp_existed = fs::exists("/tmp/nexus.db", ec);
    auto tmp_mtime = tmp_existed ? fs::last_write_time("/tmp/nexus.db", ec) : fs::file_time_type{};
    {
        Database bad((dir / "blocker" / "sub" / "nexus.db").string());
        assert(bad.open());  // in-memory fallback
    }
    assert(!fs::exists(dir / ".nexus.db"));
    assert(fs::exists("/tmp/nexus.db", ec) == tmp_existed);
    if (tmp_existed) assert(fs::last_write_time("/tmp/nexus.db", ec) == tmp_mtime);
    fs::current_path(old_cwd);
    fs::remove_all(dir, ec);

    // clear_history: empties usage_events + query_history, keeps applications; second call removes 0
    {
        Database h(":memory:");
        assert(h.open());
        DesktopEntry e;
        e.id = "keep";
        e.name = "Keep";
        e.exec = "keep";
        assert(h.save_applications({e}));
        assert(h.record_usage("keep", "k") && h.record_usage("keep", "ke") && h.record_query("k", "keep", "applications"));
        const auto cleared = h.clear_history();
        assert(cleared && cleared->usage_events == 2 && cleared->queries == 1);
        assert(h.get_usage_count("keep") == 0 && h.get_usage_counts("k").empty());
        assert(h.load_applications().size() == 1);
        const auto again = h.clear_history();
        assert(again && again->usage_events == 0 && again->queries == 0);
        Database closed(":memory:");
        assert(!closed.clear_history());  // never opened
    }

    // clear_history wipes the query text from the raw file bytes (P8.18)
    {
        const std::string marker = "zq_secret_marker_123_wipe_check";
        const fs::path d = fs::temp_directory_path() / ("nexus_wipe_" + std::to_string(getpid()));
        fs::create_directories(d);
        const fs::path dbfile = d / "nexus.db";
        auto slurp = [&](const fs::path& f) {
            std::ifstream in(f, std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(in), {});
        };
        auto leaks = [&] {
            for (const char* suffix : {"", "-journal", "-wal"}) {
                const fs::path f = fs::path(dbfile.string() + suffix);
                if (fs::exists(f) && slurp(f).find(marker) != std::string::npos) return true;
            }
            return false;
        };
        Database w(dbfile.string());
        assert(w.open());
        DesktopEntry e;
        e.id = "wipe";
        e.name = "Wipe";
        e.exec = "wipe";
        assert(w.save_applications({e}));
        // Older free pages full of the marker, left by a delete without secure_delete (what 0.4.0's clear
        // or migration 4's DROP INDEX did on SQLite builds without SQLITE_SECURE_DELETE). Forced OFF here so
        // the test doesn't depend on how the distro compiled SQLite.
        sqlite3* raw = nullptr;
        assert(sqlite3_open(dbfile.c_str(), &raw) == SQLITE_OK);
        assert(sqlite3_exec(raw, "PRAGMA secure_delete=OFF; BEGIN;", nullptr, nullptr, nullptr) == SQLITE_OK);
        for (int i = 0; i < 500; ++i) {
            const std::string sql = "INSERT INTO usage_events(app_id, query) VALUES('wipe', '" + marker + "');"
                                    "INSERT INTO query_history(query, selected_id) VALUES('" + marker + "', 'wipe');";
            assert(sqlite3_exec(raw, sql.c_str(), nullptr, nullptr, nullptr) == SQLITE_OK);
        }
        assert(sqlite3_exec(raw, "COMMIT; DELETE FROM usage_events; DELETE FROM query_history;", nullptr, nullptr,
                            nullptr) == SQLITE_OK);
        sqlite3_close(raw);
        assert(leaks());  // stale free pages hold the marker before the clear
        // plus live rows written the normal way
        assert(w.record_usage("wipe", marker) && w.record_query(marker, "wipe", "applications"));
        const auto c = w.clear_history();
        assert(c && c->usage_events == 1 && c->queries == 1 && c->file_compacted);
        assert(!leaks());
        fs::remove_all(d);
    }

    std::cout << "All Database tests passed successfully!\n";
    return 0;
}

