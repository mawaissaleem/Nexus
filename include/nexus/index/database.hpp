#pragma once

#include "nexus/index/desktop_entry.hpp"
#include <string>
#include <vector>
#include <memory>
#include <mutex>

struct sqlite3;

namespace nexus::index {

class Database {
public:
    explicit Database(std::string db_path = "");
    ~Database();

    bool open();
    void close();
    [[nodiscard]] bool is_open() const noexcept { return db_ != nullptr; }

    // Initialize tables and apply migrations
    bool init_schema();

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
    std::string db_path_;
    sqlite3* db_{nullptr};
    mutable std::mutex mutex_;
};

} // namespace nexus::index

