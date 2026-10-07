#pragma once

#include "nexus/index/desktop_entry.hpp"
#include "nexus/index/file_entry.hpp"
#include "nexus/index/database_migrations.hpp"
#include <cstdint>
#include <string>
#include <vector>
#include <unordered_map>
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

    // File index. replace_files swaps the whole table in one transaction (readers never see a half index);
    // it holds the DB mutex for that transaction only. find_files returns every row whose name contains `pattern`'s
    // UTF-8 characters in order, case-folding ASCII only (as FuzzyMatcher does); the caller scores them. It accepts
    // everything FuzzyMatcher accepts except byte-level matches that would split a multi-byte character.
    bool replace_files(const std::vector<FileEntry>& entries);
    std::vector<FileEntry> find_files(const std::string& pattern);
    // Incremental update in one transaction: first deletes every path in `removed_trees` together with everything
    // below it, then inserts or updates `upserts` (an existing row keeps its is_root flag). Used by FileWatcher.
    bool apply_file_changes(const std::vector<std::string>& removed_trees, const std::vector<FileEntry>& upserts);

    // Usage & ranking learning
    bool record_usage(const std::string& app_id, const std::string& query_text);
    int get_usage_count(const std::string& app_id);
    // Usage per app_id in one query, only from past queries that are a prefix of `query` or start
    // with it (case/space-insensitive): event count and the newest event's time (Unix seconds, UTC).
    // Apps with no such events are absent.
    struct UsageStat {
        int count{0};
        std::int64_t last_used{0};
    };
    std::unordered_map<std::string, UsageStat> get_usage_counts(const std::string& query);

    // Query history
    bool record_query(const std::string& query_text, const std::string& selected_id, const std::string& provider_id);

    // Deletes every usage_events and query_history row in one transaction (applications and files are untouched).
    // Returns the removed row counts, or nullopt if the database is closed or the delete failed (nothing removed).
    struct ClearedHistory {
        int usage_events{0};
        int queries{0};
        bool file_compacted{false};  // VACUUM succeeded: old text is gone from free pages too
    };
    std::optional<ClearedHistory> clear_history();

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

