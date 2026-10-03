#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace nexus::utils {

// One directory-entry change. `mask` holds raw inotify bits (IN_CREATE, IN_DELETE, IN_MOVED_FROM, IN_MOVED_TO,
// IN_ISDIR); `dir` is the path exactly as it was given to add_watch().
struct FsEvent {
    std::string dir;
    std::string name;
    uint32_t mask{0};
};

// Qt-free, Linux-only (raw inotify). Watches single directories (not recursive: the owner adds a watch per
// directory) and reports create/delete/move events in debounced batches on its own thread.
class FsWatcher {
public:
    enum class AddResult { Ok, LimitReached, Failed };
    using EventsHandler = std::function<void(const std::vector<FsEvent>&)>;

    // `on_events` runs on the watcher thread (it may call add_watch / remove_watches_under); exceptions are caught
    // and logged. `on_overflow` runs on the watcher thread when the kernel queue overflowed (events were lost).
    // `max_watches` > 0 caps the watches this object holds (on top of the kernel's fs.inotify.max_user_watches).
    FsWatcher(EventsHandler on_events, std::function<void()> on_overflow, size_t max_watches = 0);
    ~FsWatcher();
    FsWatcher(const FsWatcher&) = delete;
    FsWatcher& operator=(const FsWatcher&) = delete;

    // Creates the inotify instance and the thread. While `paused`, events are collected but not delivered
    // until resume(). Returns false (and logs) if the OS refuses.
    bool start(bool paused);
    void resume();
    // Wakes the thread and joins it; returns promptly. Safe to call twice.
    void stop();

    // Symlinks are not followed. LimitReached is logged once (with how to raise the system limit) and sticky:
    // later calls return it without trying. Failed covers ENOENT, EACCES, not-a-directory.
    AddResult add_watch(const std::string& dir);
    // Drops the watch on `dir` and on every watched path below it.
    void remove_watches_under(const std::string& dir);

    [[nodiscard]] bool limit_reached() const noexcept { return limit_reached_.load(); }
    [[nodiscard]] size_t watch_count() const;

private:
    void run();
    void read_events(std::vector<FsEvent>& pending);

    EventsHandler on_events_;
    std::function<void()> on_overflow_;
    size_t max_watches_;

    int inotify_fd_{-1};
    int wake_fd_{-1};
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> limit_reached_{false};

    mutable std::mutex mutex_;                     // guards watches_ (add_watch + insert must be atomic vs. the reader)
    std::unordered_map<int, std::string> watches_;  // wd -> path
};

} // namespace nexus::utils
