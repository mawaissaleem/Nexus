#include "nexus/index/file_watcher.hpp"
#include "nexus/utils/logger.hpp"
#include <filesystem>
#include <sys/inotify.h>

namespace nexus::index {

namespace fs = std::filesystem;

FileWatcher::FileWatcher(std::shared_ptr<Database> db, std::function<void()> on_overflow, size_t max_watches)
    : db_(std::move(db)), on_overflow_(std::move(on_overflow)), max_watches_(max_watches) {}

FileWatcher::~FileWatcher() { stop(); }

// Join first, release after: the watcher thread uses watcher_, levels_ and options_ inside handle().
void FileWatcher::stop() {
    stopping_ = true;  // lets a handle() busy with a large moved-in tree return early
    if (watcher_) watcher_->stop();
    watcher_.reset();
    levels_.clear();
}

void FileWatcher::resume() {
    if (watcher_) watcher_->resume();
}

void FileWatcher::arm(const FileIndexOptions& options, const std::atomic<bool>& cancel) {
    stop();
    stopping_ = false;
    if (!db_) return;
    options_ = options;
    watcher_ = std::make_unique<utils::FsWatcher>(
        [this](const std::vector<utils::FsEvent>& events) { handle(events); }, on_overflow_, max_watches_);
    if (!watcher_->start(true)) {
        watcher_.reset();
        return;
    }
    for (const auto& dir : options_.directories) {
        std::error_code ec;
        if (cancel.load()) break;
        if (fs::is_directory(dir, ec)) watch_tree(dir, 0, cancel, nullptr);
    }
    NEXUS_LOG_INFO("File watching armed: " + std::to_string(watcher_->watch_count()) + " directories");
}

// Watches `dir` first and lists it afterwards, so nothing created in between can be missed. Appends the listed
// entries to `out` when given. Symlinked directories are indexed but, like in the rebuild, never entered.
void FileWatcher::watch_tree(const std::string& dir, size_t level, const std::atomic<bool>& cancel,
                             std::vector<FileEntry>* out) {
    if (cancel.load()) return;
    if (watcher_->add_watch(dir) == utils::FsWatcher::AddResult::Ok) {
        const auto [it, inserted] = levels_.emplace(dir, level);
        if (!inserted && level < it->second) it->second = level;
    }
    std::vector<FileEntry> children;
    if (!scan_directory(dir, options_, 0, cancel, children)) return;
    for (const auto& child : children) {
        std::error_code ec;
        if (child.is_dir && level < options_.max_depth && !fs::is_symlink(child.path, ec)) {
            watch_tree(child.path, level + 1, cancel, out);
        }
        if (out) out->push_back(child);
    }
}

void FileWatcher::handle(const std::vector<utils::FsEvent>& events) {
    std::vector<std::string> removed;
    std::vector<FileEntry> upserts;

    for (const auto& ev : events) {
        if (is_skipped_name(ev.name, options_.exclusions)) continue;
        const std::string path = (fs::path(ev.dir) / ev.name).string();  // same form as the rebuild's walk

        if (ev.mask & (IN_DELETE | IN_MOVED_FROM)) {
            removed.push_back(path);
            watcher_->remove_watches_under(path);
            const std::string prefix = path + "/";
            for (auto it = levels_.begin(); it != levels_.end();) {
                it = (it->first == path || it->first.compare(0, prefix.size(), prefix) == 0) ? levels_.erase(it) : std::next(it);
            }
        }
        if (ev.mask & (IN_CREATE | IN_MOVED_TO)) {
            // Trust the disk, not the event: it may be gone again, or already covered by a rebuild.
            std::error_code ec;
            if (!fs::exists(fs::symlink_status(path, ec)) || ec) {
                removed.push_back(path);
                continue;
            }
            const bool is_dir = fs::is_directory(path, ec);
            upserts.push_back({path, ev.name, is_dir, false});
            const auto parent = levels_.find(ev.dir);
            if (is_dir && parent != levels_.end() && parent->second < options_.max_depth && !fs::is_symlink(path, ec)) {
                watch_tree(path, parent->second + 1, stopping_, &upserts);  // also indexes what is already inside
            }
        }
    }
    if (!removed.empty() || !upserts.empty()) db_->apply_file_changes(removed, upserts);
}

} // namespace nexus::index
