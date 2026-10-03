#pragma once

#include "nexus/index/database.hpp"
#include "nexus/index/file_indexer.hpp"
#include "nexus/utils/fs_watcher.hpp"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace nexus::index {

// Keeps the `files` table current between rebuilds: one inotify watch per in-scope directory, events turned into
// incremental Database updates (batched, one transaction each). Qt-free; works on its own thread.
//
// Protocol around a full rebuild (callers serialise arm/resume/stop; never call them from `on_overflow`):
//     arm(options, cancel);  indexer.rebuild(options, cancel);  resume();
// arm() watches first and holds events, the rebuild then walks the disk, so every change is either already in the
// walk or a held event. Events are applied by looking at the disk, not by trusting the event type, so replaying one
// the rebuild already saw is harmless.
//
// Limits: the root directories themselves being deleted or moved is not noticed (their rows stay until the next
// rebuild); with nested roots a directory takes the smallest depth; directories past the watch limit are only
// refreshed by a rebuild.
class FileWatcher {
public:
    // `on_overflow` is called (on the watcher thread) when the kernel dropped events; the owner should schedule a
    // full rebuild through its normal rebuild path. `max_watches` > 0 caps watches (see FsWatcher).
    FileWatcher(std::shared_ptr<Database> db, std::function<void()> on_overflow, size_t max_watches = 0);
    ~FileWatcher();

    // Replaces any previous watching with watches matching `options`, events held until resume().
    void arm(const FileIndexOptions& options, const std::atomic<bool>& cancel);
    void resume();
    void stop();

    // Read watcher_ unsynchronised: call only from the thread that calls arm()/stop().
    [[nodiscard]] bool limit_reached() const noexcept { return watcher_ && watcher_->limit_reached(); }
    [[nodiscard]] size_t watch_count() const { return watcher_ ? watcher_->watch_count() : 0; }

private:
    void watch_tree(const std::string& dir, size_t level, const std::atomic<bool>& cancel, std::vector<FileEntry>* out);
    void handle(const std::vector<utils::FsEvent>& events);

    std::shared_ptr<Database> db_;
    std::function<void()> on_overflow_;
    size_t max_watches_;
    FileIndexOptions options_;
    std::unordered_map<std::string, size_t> levels_;  // watched dir -> depth below its root (root = 0); watcher thread
                                                       // only, except inside arm() while events are held
    std::atomic<bool> stopping_{false};  // cancel token for handle(), set by stop()
    std::unique_ptr<utils::FsWatcher> watcher_;
};

} // namespace nexus::index
