#include "nexus/index/database.hpp"
#include "nexus/utils/logger.hpp"
#include <sqlite3.h>
#include <filesystem>
#include <cstdlib>
#include <sstream>

namespace nexus::index {

namespace {

std::string join_strings(const std::vector<std::string>& list, char delim) {
    std::string result;
    for (size_t i = 0; i < list.size(); ++i) {
        if (i > 0) result.push_back(delim);
        result.append(list[i]);
    }
    return result;
}

std::vector<std::string> split_string(const std::string& s, char delim) {
    std::vector<std::string> tokens;
    std::string token;
    std::istringstream tokenStream(s);
    while (std::getline(tokenStream, token, delim)) {
        if (!token.empty()) tokens.push_back(token);
    }
    return tokens;
}

} // namespace

Database::Database(std::string db_path)
    : db_path_(std::move(db_path)) {
    if (db_path_.empty()) {
        db_path_ = default_db_path();
    }
}

Database::~Database() {
    close();
}

std::string Database::default_db_path() {
    const char* xdg_data = std::getenv("XDG_DATA_HOME");
    std::filesystem::path base;
    if (xdg_data && *xdg_data) {
        base = xdg_data;
    } else {
        const char* home = std::getenv("HOME");
        base = home ? std::filesystem::path(home) / ".local" / "share" : std::filesystem::path("/tmp");
    }
    std::filesystem::path dir = base / "nexus";
    return (dir / "nexus.db").string();
}

bool Database::open() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) return true;

    auto try_open_path = [this](const std::string& path) -> bool {
        if (path != ":memory:") {
            std::filesystem::path p(path);
            if (p.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(p.parent_path(), ec);
                if (ec) return false;
            }
        }
        int rc = sqlite3_open(path.c_str(), &db_);
        if (rc == SQLITE_OK) {
            db_path_ = path;
            return true;
        }
        if (db_) {
            sqlite3_close(db_);
            db_ = nullptr;
        }
        return false;
    };

    if (try_open_path(db_path_)) {
        NEXUS_LOG_DEBUG("Opened database at " + db_path_);
        return init_schema();
    }

    // Fallback 1: Local .nexus.db in current working directory
    if (try_open_path(".nexus.db")) {
        NEXUS_LOG_INFO("Falling back to local database: " + db_path_);
        return init_schema();
    }

    // Fallback 2: /tmp/nexus.db
    if (try_open_path("/tmp/nexus.db")) {
        NEXUS_LOG_INFO("Falling back to temp database: " + db_path_);
        return init_schema();
    }

    // Fallback 3: In-memory
    if (try_open_path(":memory:")) {
        NEXUS_LOG_INFO("Falling back to in-memory database");
        return init_schema();
    }

    NEXUS_LOG_ERROR("Failed to initialize any SQLite database backend.");
    return false;
}

void Database::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

bool Database::init_schema() {
    if (!db_) return false;

    const char* schema_sql = R"(
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER PRIMARY KEY
        );

        CREATE TABLE IF NOT EXISTS applications (
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

        CREATE TABLE IF NOT EXISTS usage_events (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            app_id TEXT NOT NULL,
            query TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE TABLE IF NOT EXISTS query_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            query TEXT NOT NULL,
            selected_id TEXT,
            provider_id TEXT,
            timestamp DATETIME DEFAULT CURRENT_TIMESTAMP
        );

        CREATE INDEX IF NOT EXISTS idx_usage_app_id ON usage_events(app_id);
    )";

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, schema_sql, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        NEXUS_LOG_ERROR("Failed to initialize database schema: " + std::string(err_msg ? err_msg : "unknown error"));
        if (err_msg) sqlite3_free(err_msg);
        return false;
    }

    return true;
}

bool Database::save_applications(const std::vector<DesktopEntry>& entries) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr);

    const char* insert_sql = R"(
        INSERT OR REPLACE INTO applications (
            id, name, generic_name, comment, exec, icon, desktop_file, categories, keywords, terminal
        ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, insert_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        NEXUS_LOG_ERROR("Failed to prepare statement for save_applications: " + std::string(sqlite3_errmsg(db_)));
        sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, nullptr);
        return false;
    }

    for (const auto& entry : entries) {
        sqlite3_bind_text(stmt, 1, entry.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, entry.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, entry.generic_name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, entry.comment.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, entry.exec.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, entry.icon.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 7, entry.desktop_file.c_str(), -1, SQLITE_TRANSIENT);

        std::string cats = join_strings(entry.categories, ';');
        sqlite3_bind_text(stmt, 8, cats.c_str(), -1, SQLITE_TRANSIENT);

        std::string keys = join_strings(entry.keywords, ';');
        sqlite3_bind_text(stmt, 9, keys.c_str(), -1, SQLITE_TRANSIENT);

        sqlite3_bind_int(stmt, 10, entry.terminal ? 1 : 0);

        if (sqlite3_step(stmt) != SQLITE_DONE) {
            NEXUS_LOG_WARN("Failed inserting application: " + entry.id);
        }
        sqlite3_reset(stmt);
    }

    sqlite3_finalize(stmt);
    sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, nullptr);
    return true;
}

std::vector<DesktopEntry> Database::load_applications() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DesktopEntry> results;
    if (!db_) return results;

    const char* select_sql = R"(
        SELECT id, name, generic_name, comment, exec, icon, desktop_file, categories, keywords, terminal
        FROM applications;
    )";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, select_sql, -1, &stmt, nullptr) != SQLITE_OK) {
        NEXUS_LOG_ERROR("Failed to load applications: " + std::string(sqlite3_errmsg(db_)));
        return results;
    }

    auto get_text = [](sqlite3_stmt* s, int col) -> std::string {
        const unsigned char* val = sqlite3_column_text(s, col);
        return val ? reinterpret_cast<const char*>(val) : "";
    };

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        DesktopEntry entry;
        entry.id = get_text(stmt, 0);
        entry.name = get_text(stmt, 1);
        entry.generic_name = get_text(stmt, 2);
        entry.comment = get_text(stmt, 3);
        entry.exec = get_text(stmt, 4);
        entry.icon = get_text(stmt, 5);
        entry.desktop_file = get_text(stmt, 6);
        entry.categories = split_string(get_text(stmt, 7), ';');
        entry.keywords = split_string(get_text(stmt, 8), ';');
        entry.terminal = sqlite3_column_int(stmt, 9) != 0;
        results.push_back(std::move(entry));
    }

    sqlite3_finalize(stmt);
    return results;
}

bool Database::record_usage(const std::string& app_id, const std::string& query_text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = "INSERT INTO usage_events (app_id, query) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, app_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, query_text.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

int Database::get_usage_count(const std::string& app_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return 0;

    const char* sql = "SELECT COUNT(*) FROM usage_events WHERE app_id = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return 0;

    sqlite3_bind_text(stmt, 1, app_id.c_str(), -1, SQLITE_TRANSIENT);

    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return count;
}

bool Database::record_query(const std::string& query_text, const std::string& selected_id, const std::string& provider_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!db_) return false;

    const char* sql = "INSERT INTO query_history (query, selected_id, provider_id) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    sqlite3_bind_text(stmt, 1, query_text.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, selected_id.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, provider_id.c_str(), -1, SQLITE_TRANSIENT);

    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace nexus::index
