#pragma once

#include "nexus/index/desktop_entry.hpp"
#include "nexus/index/database_migrations.hpp"
#include <string>
#include <vector>
#include <memory>
#include <mutex>
#include <optional>

struct sqlite3;

namespace nexus::index {

class Database {
public:
    explicit Database(std::string db_path = "");
    ~Database();

    bool open();
    void close();
    [[nodiscard]] bool is_open() const noexcept { return db_ != nullptr; }

    // Read current schema version (returns 0 when DB is closed, empty, or uninitialized)
    [[nodiscard]] int schema_version() const;

    // Application persistence
    bool save_applications(const std::vector<DesktopEntry>& entries);
    std::vector<DesktopEntry> load_applications();

    // Usage & ranking learning
    bool record_usage(const std::string& app_id, const std::string& query_text);
    int get_usage_count(const std::string& app_id);

    // Query history
    bool record_query(const std::string& query_text, const std::string& selected_id, const std::string& provider_id);

    // Get default XDG database path (~/.local/share/nexus/nexus.db)
    static std::string default_db_path();

private:
    // Initialize tables and apply migrations (caller must hold mutex_)
    bool init_schema();

    std::string db_path_;
    sqlite3* db_{nullptr};
    mutable std::mutex mutex_;
    std::optional<std::vector<Migration>> custom_migrations_;

    friend void internal::set_database_migrations_for_testing(Database& db, std::vector<Migration> migrations);
};

} // namespace nexus::index

