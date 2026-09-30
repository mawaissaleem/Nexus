#pragma once

#include <string>
#include <vector>
#include <functional>

struct sqlite3;

namespace nexus::index {

class Database;

struct Migration {
    int version{0};
    std::string description;
    std::string sql;
    std::function<bool(sqlite3*)> step{nullptr};

    Migration() = default;
    Migration(int v, std::string desc, std::string s, std::function<bool(sqlite3*)> fn = nullptr)
        : version(v), description(std::move(desc)), sql(std::move(s)), step(std::move(fn)) {}
};

// Validates that migrations are strictly increasing and contiguous from 1.
bool validate_migrations(const std::vector<Migration>& migrations);

// Returns the current schema version (-1 on query error, 0 if empty or table has no rows).
int get_schema_version(sqlite3* db);

// Runs the given migrations on db transactionally.
bool run_migrations(sqlite3* db, const std::vector<Migration>& migrations);

// Returns the default Nexus migrations list.
const std::vector<Migration>& default_migrations();

namespace internal {
// Test hook: inject custom migrations into a Database instance before open().
void set_database_migrations_for_testing(Database& db, std::vector<Migration> migrations);
}

} // namespace nexus::index
