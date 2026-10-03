#include "nexus/index/file_indexer.hpp"
#include "nexus/utils/logger.hpp"
#include <algorithm>
#include <filesystem>

namespace nexus::index {

FileIndexer::FileIndexer(std::shared_ptr<Database> db)
    : db_(std::move(db)) {}

bool is_skipped_name(const std::string& name, const std::vector<std::string>& exclusions) {
    return name.empty() || name.front() == '.' ||
           std::find(exclusions.begin(), exclusions.end(), name) != exclusions.end();
}

bool scan_directory(const std::string& dir, const FileIndexOptions& options, size_t remaining_depth,
                    const std::atomic<bool>& cancel, std::vector<FileEntry>& out) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::recursive_directory_iterator it(fs::path(dir), fs::directory_options::skip_permission_denied, ec);
    const fs::recursive_directory_iterator end_it;
    while (it != end_it && !ec) {
        if (cancel.load()) return false;

        const std::string name = it->path().filename().string();
        const bool is_dir = it->is_directory(ec);
        if (is_skipped_name(name, options.exclusions)) {
            if (is_dir) it.disable_recursion_pending();
        } else {
            out.push_back({it->path().string(), name, is_dir, false});
            if (is_dir && static_cast<size_t>(it.depth()) >= remaining_depth) it.disable_recursion_pending();
        }
        it.increment(ec);
    }
    return true;
}

size_t FileIndexer::rebuild(const FileIndexOptions& options, const std::atomic<bool>& cancel) {
    namespace fs = std::filesystem;
    if (!db_) return 0;

    std::vector<FileEntry> entries;
    for (const auto& dir : options.directories) {
        fs::path base(dir);
        std::error_code ec;
        if (!fs::is_directory(base, ec)) continue;

        if (!base.filename().empty()) {
            entries.push_back({base.string(), base.filename().string(), true, true});
        }
        if (!scan_directory(dir, options, options.max_depth, cancel, entries)) return 0;
    }

    if (cancel.load() || !db_->replace_files(entries)) return 0;
    NEXUS_LOG_INFO("File index rebuilt: " + std::to_string(entries.size()) + " entries");
    return entries.size();
}

} // namespace nexus::index
