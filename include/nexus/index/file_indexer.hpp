#pragma once

#include "nexus/index/database.hpp"
#include <atomic>
#include <memory>
#include <string>
#include <vector>

namespace nexus::index {

struct FileIndexOptions {
    std::vector<std::string> directories;  // scopes: the configured search directories
    std::vector<std::string> exclusions;   // exact entry names to skip (hidden dot-names are always skipped)
    size_t max_depth{3};                   // 0 = only direct children of each directory
};

// The scope rules, shared with FileWatcher so incremental updates index exactly what a rebuild does.
// Hidden dot-names and configured exclusions are skipped (and not descended into).
bool is_skipped_name(const std::string& name, const std::vector<std::string>& exclusions);
// Appends the entries below `dir` (not `dir` itself) to `out`, descending `remaining_depth` directory levels below
// it (0 = direct children only). Returns false if `cancel` fired.
bool scan_directory(const std::string& dir, const FileIndexOptions& options, size_t remaining_depth,
                    const std::atomic<bool>& cancel, std::vector<FileEntry>& out);

class FileIndexer {
public:
    explicit FileIndexer(std::shared_ptr<Database> db);

    // Walks the directories (no DB lock held) and then swaps the files table in one transaction.
    // Returns rows written. If `cancel` fires, or the write fails, returns 0 and the old index stays untouched.
    size_t rebuild(const FileIndexOptions& options, const std::atomic<bool>& cancel);

private:
    std::shared_ptr<Database> db_;
};

} // namespace nexus::index
