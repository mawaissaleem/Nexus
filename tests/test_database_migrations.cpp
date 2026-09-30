#include "nexus/index/database.hpp"
#include "nexus/index/database_migrations.hpp"
#include <sqlite3.h>
#include <iostream>
#include <cassert>
#include <filesystem>
#include <chrono>
#include <atomic>
#include <string>
#include <vector>

namespace {

struct TempDbFile {
    std::filesystem::path path;

    TempDbFile() {
        static std::atomic<uint64_t> counter{0};
        auto now = std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
               ("nexus_test_migration_" + std::to_string(now) + "_" + std::to_string(++counter) + ".db");
    }

    ~TempDbFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    std::string string() const { return path.string(); }
};

int raw_query_int(const std::string& path, const std::string& sql) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    assert(rc == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    int val = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        val = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return val;
}

std::string raw_query_text(const std::string& path, const std::string& sql) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    assert(rc == SQLITE_OK);
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    std::string val;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char* text = sqlite3_column_text(stmt, 0);
        if (text) val = reinterpret_cast<const char*>(text);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return val;
}

bool raw_table_exists(const std::string& path, const std::string& table_name) {
    sqlite3* db = nullptr;
    int rc = sqlite3_open(path.c_str(), &db);
    assert(rc == SQLITE_OK);
    const char* sql = "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name=?;";
    sqlite3_stmt* stmt = nullptr;
    rc = sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    assert(rc == SQLITE_OK);
    sqlite3_bind_text(stmt, 1, table_name.c_str(), -1, SQLITE_TRANSIENT);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    sqlite3_close(db);
    return count > 0;
}

void test_fresh_db() {
    std::cout << "Running test_fresh_db...\n";
    TempDbFile temp_file;
    {
        nexus::index::Database db(temp_file.string());
        assert(db.open());
        assert(db.is_open());
        assert(db.schema_version() == 1);
        db.close();
        assert(!db.is_open());
        assert(db.schema_version() == 0);
    }

    assert(raw_table_exists(temp_file.string(), "schema_version"));
    assert(raw_table_exists(temp_file.string(), "applications"));
    assert(raw_table_exists(temp_file.string(), "usage_events"));
    assert(raw_table_exists(temp_file.string(), "query_history"));
    assert(raw_query_int(temp_file.string(), "SELECT COUNT(*) FROM schema_version;") == 1);
    assert(raw_query_int(temp_file.string(), "SELECT version FROM schema_version;") == 1);
}

void test_legacy_db() {
    std::cout << "Running test_legacy_db...\n";
    TempDbFile temp_file;

    // Create legacy DB fixture with populated tables and empty schema_version
    {
        sqlite3* db = nullptr;
        int rc = sqlite3_open(temp_file.string().c_str(), &db);
        assert(rc == SQLITE_OK);

        const char* legacy_schema = R"(
            CREATE TABLE schema_version (
                version INTEGER PRIMARY KEY
            );

            CREATE TABLE applications (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                generic_name TEXT,
                comment TEXT,
                exec TEXT NOT NULL,
                icon TEXT,
                desktop_file TEXT NOT NULL,
                categories TEXT,
                keywords TEXT,
                terminal INTEGER DEFAULT 0
            );

            CREATE TABLE usage_events (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                app_id TEXT NOT NULL,
                query TEXT,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE TABLE query_history (
                id INTEGER PRIMARY KEY AUTOINCREMENT,
                query TEXT NOT NULL,
                selected_id TEXT,
                provider_id TEXT,
                timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
            );

            CREATE INDEX idx_usage_app_id ON usage_events(app_id);

            INSERT INTO applications (id, name, generic_name, comment, exec, icon, desktop_file, categories, keywords, terminal)
            VALUES ('legacy.app.1', 'Legacy App One', 'Utility Tool', 'A legacy tool', 'legacy-app1 --gui', 'legacy1.png', '/usr/share/applications/legacy1.desktop', 'Utility;Office;', 'legacy;tool;', 0);

            INSERT INTO applications (id, name, exec, desktop_file)
            VALUES ('legacy.app.2', 'Legacy App Two', 'legacy-app2', '/usr/share/applications/legacy2.desktop');

            INSERT INTO usage_events (app_id, query) VALUES ('legacy.app.1', 'legacy');
            INSERT INTO usage_events (app_id, query) VALUES ('legacy.app.1', 'one');

            INSERT INTO query_history (query, selected_id, provider_id) VALUES ('legacy', 'legacy.app.1', 'applications');
            INSERT INTO query_history (query, selected_id, provider_id) VALUES ('two', 'legacy.app.2', 'applications');
        )";

        char* err = nullptr;
        rc = sqlite3_exec(db, legacy_schema, nullptr, nullptr, &err);
        assert(rc == SQLITE_OK);
        sqlite3_close(db);
    }

    assert(raw_query_int(temp_file.string(), "SELECT COUNT(*) FROM schema_version;") == 0);

    // Open via Database
    {
        nexus::index::Database db(temp_file.string());
        assert(db.open());
        assert(db.is_open());
        assert(db.schema_version() == 1);

        // Verify data intact
        auto apps = db.load_applications();
        assert(apps.size() == 2);
        bool found_app1 = false;
        for (const auto& app : apps) {
            if (app.id == "legacy.app.1") {
                found_app1 = true;
                assert(app.name == "Legacy App One");
                assert(app.generic_name == "Utility Tool");
                assert(app.exec == "legacy-app1 --gui");
                assert(app.icon == "legacy1.png");
                assert(app.categories.size() == 2);
                assert(app.categories[0] == "Utility");
                assert(app.categories[1] == "Office");
            }
        }
        assert(found_app1);

        assert(db.get_usage_count("legacy.app.1") == 2);
        assert(db.get_usage_count("legacy.app.2") == 0);

        db.close();
    }

    assert(raw_query_int(temp_file.string(), "SELECT COUNT(*) FROM query_history;") == 2);
    assert(raw_query_text(temp_file.string(), "SELECT query FROM query_history WHERE selected_id='legacy.app.1';") == "legacy");
    assert(raw_query_int(temp_file.string(), "SELECT COUNT(*) FROM schema_version;") == 1);
    assert(raw_query_int(temp_file.string(), "SELECT version FROM schema_version;") == 1);
}

void test_idempotence() {
    std::cout << "Running test_idempotence...\n";
    TempDbFile temp_file;
    {
        nexus::index::Database db(temp_file.string());
        assert(db.open());
        assert(db.schema_version() == 1);
        db.close();
    }

    {
        nexus::index::Database db(temp_file.string());
        assert(db.open());
        assert(db.schema_version() == 1);
        db.close();
    }

    assert(raw_query_int(temp_file.string(), "SELECT COUNT(*) FROM schema_version;") == 1);
    assert(raw_query_int(temp_file.string(), "SELECT version FROM schema_version;") == 1);
}

void test_multistep_migrations() {
    std::cout << "Running test_multistep_migrations...\n";
    TempDbFile temp_file;

    std::vector<nexus::index::Migration> migrations = {
        nexus::index::default_migrations()[0],
        {2, "add test column to applications", "ALTER TABLE applications ADD COLUMN custom_tag TEXT DEFAULT 'default_tag';"},
        {3, "add audit table", "CREATE TABLE audit_log (id INTEGER PRIMARY KEY, note TEXT);"}
    };

    // Case 1: Multi-step migration from fresh DB to version 3
    {
        nexus::index::Database db(temp_file.string());
        nexus::index::internal::set_database_migrations_for_testing(db, migrations);
        assert(db.open());
        assert(db.schema_version() == 3);
        db.close();
    }

    assert(raw_query_int(temp_file.string(), "SELECT COUNT(*) FROM schema_version;") == 3);
    assert(raw_query_int(temp_file.string(), "SELECT MAX(version) FROM schema_version;") == 3);
    assert(raw_table_exists(temp_file.string(), "audit_log"));

    // Case 2: Multi-step incremental from existing version 1 to version 3
    TempDbFile temp_file2;
    {
        nexus::index::Database db(temp_file2.string());
        assert(db.open());
        assert(db.schema_version() == 1);
        db.close();
    }
    assert(raw_query_int(temp_file2.string(), "SELECT MAX(version) FROM schema_version;") == 1);

    {
        nexus::index::Database db(temp_file2.string());
        nexus::index::internal::set_database_migrations_for_testing(db, migrations);
        assert(db.open());
        assert(db.schema_version() == 3);
        db.close();
    }
    assert(raw_query_int(temp_file2.string(), "SELECT MAX(version) FROM schema_version;") == 3);
    assert(raw_query_int(temp_file2.string(), "SELECT COUNT(*) FROM schema_version;") == 3);
    assert(raw_table_exists(temp_file2.string(), "audit_log"));
}

void test_failure_rollback() {
    std::cout << "Running test_failure_rollback...\n";
    TempDbFile temp_file;

    // Establish DB at version 1 with data
    {
        nexus::index::Database db(temp_file.string());
        assert(db.open());
        assert(db.schema_version() == 1);

        nexus::index::DesktopEntry entry;
        entry.id = "survivor.app";
        entry.name = "Survivor App";
        entry.exec = "survivor";
        entry.desktop_file = "/path/to/survivor";
        assert(db.save_applications({entry}));
        db.close();
    }

    std::vector<nexus::index::Migration> injected = {
        nexus::index::default_migrations()[0],
        {2, "failing migration", "CREATE TABLE temp_table_should_rollback (id INT); INVALID SQL SYNTAX ERROR;"},
        {3, "subsequent migration", "CREATE TABLE should_never_exist (id INT);"}
    };

    {
        nexus::index::Database db(temp_file.string());
        nexus::index::internal::set_database_migrations_for_testing(db, injected);
        assert(!db.open());
        assert(!db.is_open());
    }

    // Verify rollback: partial changes rolled back, version not bumped, later migrations did not run
    assert(raw_query_int(temp_file.string(), "SELECT MAX(version) FROM schema_version;") == 1);
    assert(raw_query_int(temp_file.string(), "SELECT COUNT(*) FROM schema_version;") == 1);
    assert(!raw_table_exists(temp_file.string(), "temp_table_should_rollback"));
    assert(!raw_table_exists(temp_file.string(), "should_never_exist"));

    // Verify existing data intact
    {
        nexus::index::Database db(temp_file.string());
        assert(db.open());
        auto apps = db.load_applications();
        assert(apps.size() == 1);
        assert(apps[0].id == "survivor.app");
        db.close();
    }
}

void test_newer_than_binary() {
    std::cout << "Running test_newer_than_binary...\n";
    TempDbFile temp_file;

    {
        sqlite3* db = nullptr;
        int rc = sqlite3_open(temp_file.string().c_str(), &db);
        assert(rc == SQLITE_OK);

        const char* sql = R"(
            CREATE TABLE schema_version (version INTEGER PRIMARY KEY);
            INSERT INTO schema_version (version) VALUES (99);
            CREATE TABLE future_table (payload TEXT);
            INSERT INTO future_table (payload) VALUES ('preserved_future_data');
        )";
        char* err = nullptr;
        rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
        assert(rc == SQLITE_OK);
        sqlite3_close(db);
    }

    {
        nexus::index::Database db(temp_file.string());
        assert(!db.open());
        assert(!db.is_open());
    }

    // Contents must be completely untouched
    assert(raw_query_int(temp_file.string(), "SELECT MAX(version) FROM schema_version;") == 99);
    assert(raw_table_exists(temp_file.string(), "future_table"));
    assert(raw_query_text(temp_file.string(), "SELECT payload FROM future_table;") == "preserved_future_data");
}

void test_invalid_migration_list() {
    std::cout << "Running test_invalid_migration_list...\n";

    // Gap: [1, 3]
    std::vector<nexus::index::Migration> gap = {
        {1, "m1", "CREATE TABLE t1 (id INT);"},
        {3, "m3", "CREATE TABLE t3 (id INT);"}
    };
    assert(!nexus::index::validate_migrations(gap));

    // Duplicate: [1, 1]
    std::vector<nexus::index::Migration> duplicate = {
        {1, "m1", "CREATE TABLE t1 (id INT);"},
        {1, "m1_dup", "CREATE TABLE t2 (id INT);"}
    };
    assert(!nexus::index::validate_migrations(duplicate));

    // Not starting at 1: [2, 3]
    std::vector<nexus::index::Migration> not_starting_at_one = {
        {2, "m2", "CREATE TABLE t2 (id INT);"},
        {3, "m3", "CREATE TABLE t3 (id INT);"}
    };
    assert(!nexus::index::validate_migrations(not_starting_at_one));

    // Empty list
    std::vector<nexus::index::Migration> empty;
    assert(!nexus::index::validate_migrations(empty));

    // Migration with empty SQL and null step function
    std::vector<nexus::index::Migration> empty_content = {
        {1, "empty content", "", nullptr}
    };
    assert(!nexus::index::validate_migrations(empty_content));

    // Database rejects invalid injected list on open
    TempDbFile temp_file;
    {
        nexus::index::Database db(temp_file.string());
        nexus::index::internal::set_database_migrations_for_testing(db, gap);
        assert(!db.open());
        assert(!db.is_open());
    }

    {
        nexus::index::Database db(temp_file.string());
        nexus::index::internal::set_database_migrations_for_testing(db, empty_content);
        assert(!db.open());
        assert(!db.is_open());
    }
}

void test_corrupted_schema_version() {
    std::cout << "Running test_corrupted_schema_version...\n";
    TempDbFile temp_file;

    // Create schema_version with invalid schema (no 'version' column)
    {
        sqlite3* db = nullptr;
        int rc = sqlite3_open(temp_file.string().c_str(), &db);
        assert(rc == SQLITE_OK);
        const char* sql = "CREATE TABLE schema_version (invalid_col TEXT);";
        rc = sqlite3_exec(db, sql, nullptr, nullptr, nullptr);
        assert(rc == SQLITE_OK);
        sqlite3_close(db);
    }

    // open() must fail cleanly and is_open() must be false
    nexus::index::Database db(temp_file.string());
    assert(!db.open());
    assert(!db.is_open());
    assert(db.schema_version() == 0);
}

void test_busy_timeout_lowered_after_open() {
    std::cout << "Running test_busy_timeout_lowered_after_open...\n";
    TempDbFile temp_file;

    nexus::index::Database db(temp_file.string());
    assert(db.open());
    assert(db.is_open());

    sqlite3* raw_db = nullptr;
    int rc = sqlite3_open(temp_file.string().c_str(), &raw_db);
    assert(rc == SQLITE_OK);

    // Acquire exclusive write lock via raw sqlite handle
    rc = sqlite3_exec(raw_db, "BEGIN EXCLUSIVE TRANSACTION;", nullptr, nullptr, nullptr);
    assert(rc == SQLITE_OK);

    auto start = std::chrono::steady_clock::now();
    // Try an operation that requires write lock on db; with 250 ms timeout, it fails quickly
    bool ok = db.record_usage("app_timeout_test", "query");
    auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start
    ).count();

    assert(!ok);
    // Confirm it returned within roughly 1 second (>= 150 ms and < 2000 ms), NOT 5000 ms
    assert(elapsed_ms >= 150);
    assert(elapsed_ms < 2000);

    sqlite3_exec(raw_db, "ROLLBACK;", nullptr, nullptr, nullptr);
    sqlite3_close(raw_db);
    db.close();
}

void test_two_instances_sequential() {
    std::cout << "Running test_two_instances_sequential...\n";
    TempDbFile temp_file;

    {
        nexus::index::Database db1(temp_file.string());
        assert(db1.open());
        assert(db1.schema_version() == 1);
        db1.close();
    }

    {
        nexus::index::Database db2(temp_file.string());
        assert(db2.open());
        assert(db2.schema_version() == 1);
        db2.close();
    }

    assert(raw_query_int(temp_file.string(), "SELECT COUNT(*) FROM schema_version;") == 1);
    assert(raw_query_int(temp_file.string(), "SELECT version FROM schema_version;") == 1);
}

} // namespace

int main() {
    test_fresh_db();
    test_legacy_db();
    test_idempotence();
    test_multistep_migrations();
    test_failure_rollback();
    test_newer_than_binary();
    test_invalid_migration_list();
    test_corrupted_schema_version();
    test_busy_timeout_lowered_after_open();
    test_two_instances_sequential();

    std::cout << "All database migration tests passed successfully!\n";
    return 0;
}
