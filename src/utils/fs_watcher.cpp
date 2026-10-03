#include "nexus/utils/fs_watcher.hpp"
#include "nexus/utils/logger.hpp"
#include <cerrno>
#include <cstring>
#include <fstream>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace nexus::utils {

namespace {

constexpr int kDebounceMs = 50;       // a burst (git checkout, unpacking) is delivered once it goes quiet for this long
constexpr size_t kMaxBatch = 4096;    // ...or when this many events are waiting, so a never-ending stream still flows

std::string system_watch_limit() {
    std::ifstream in("/proc/sys/fs/inotify/max_user_watches");
    std::string value;
    return (in >> value) ? value : "unknown";
}

} // namespace

FsWatcher::FsWatcher(EventsHandler on_events, std::function<void()> on_overflow, size_t max_watches)
    : on_events_(std::move(on_events)), on_overflow_(std::move(on_overflow)), max_watches_(max_watches) {}

FsWatcher::~FsWatcher() { stop(); }

bool FsWatcher::start(bool paused) {
    inotify_fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    wake_fd_ = ::eventfd(0, EFD_CLOEXEC);
    if (inotify_fd_ < 0 || wake_fd_ < 0) {
        NEXUS_LOG_WARN(std::string("File watching unavailable: ") + std::strerror(errno));
        stop();
        return false;
    }
    paused_ = paused;
    thread_ = std::thread([this]() { run(); });
    return true;
}

void FsWatcher::resume() {
    paused_ = false;
    if (wake_fd_ >= 0) {
        const uint64_t one = 1;
        const ssize_t n = ::write(wake_fd_, &one, sizeof one);
        (void)n;
    }
}

void FsWatcher::stop() {
    stop_ = true;
    if (thread_.joinable()) {
        const uint64_t one = 1;
        const ssize_t n = ::write(wake_fd_, &one, sizeof one);
        (void)n;
        thread_.join();
    }
    if (inotify_fd_ >= 0) ::close(inotify_fd_);
    if (wake_fd_ >= 0) ::close(wake_fd_);
    inotify_fd_ = wake_fd_ = -1;
    std::lock_guard<std::mutex> lock(mutex_);
    watches_.clear();
}

FsWatcher::AddResult FsWatcher::add_watch(const std::string& dir) {
    if (limit_reached_) return AddResult::LimitReached;
    std::lock_guard<std::mutex> lock(mutex_);
    if (inotify_fd_ < 0) return AddResult::Failed;

    int wd = -1;
    int err = 0;
    if (max_watches_ > 0 && watches_.size() >= max_watches_) {
        err = ENOSPC;
    } else {
        wd = ::inotify_add_watch(inotify_fd_, dir.c_str(),
                                 IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO | IN_ONLYDIR | IN_DONT_FOLLOW);
        err = errno;
    }
    if (wd >= 0) {
        watches_[wd] = dir;  // an already-watched (e.g. renamed) directory returns its old wd: this updates the path
        return AddResult::Ok;
    }
    if (err == ENOSPC) {
        if (!limit_reached_.exchange(true)) {
            NEXUS_LOG_WARN("File watch limit reached after " + std::to_string(watches_.size()) +
                           " directories (fs.inotify.max_user_watches = " + system_watch_limit() +
                           "). Changes in directories without a watch are only picked up by Rebuild Indexes. "
                           "Raise the limit with: sudo sysctl fs.inotify.max_user_watches=524288");
        }
        return AddResult::LimitReached;
    }
    if (err != ENOENT && err != ENOTDIR) {  // vanished directories and symlinks are normal; EACCES etc. are worth a line
        NEXUS_LOG_DEBUG("Cannot watch " + dir + ": " + std::strerror(err));
    }
    return AddResult::Failed;
}

void FsWatcher::remove_watches_under(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string prefix = dir + "/";
    for (auto it = watches_.begin(); it != watches_.end();) {
        if (it->second == dir || it->second.compare(0, prefix.size(), prefix) == 0) {
            ::inotify_rm_watch(inotify_fd_, it->first);
            it = watches_.erase(it);
        } else {
            ++it;
        }
    }
}

size_t FsWatcher::watch_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return watches_.size();
}

void FsWatcher::read_events(std::vector<FsEvent>& pending) {
    alignas(inotify_event) char buf[16384];
    ssize_t len;
    while ((len = ::read(inotify_fd_, buf, sizeof buf)) > 0) {
        for (ssize_t off = 0; off < len;) {
            const auto* ev = reinterpret_cast<const inotify_event*>(buf + off);
            off += static_cast<ssize_t>(sizeof(inotify_event) + ev->len);

            if (ev->mask & IN_Q_OVERFLOW) {
                pending.clear();  // lost events make the rest meaningless; the owner rebuilds from scratch
                if (on_overflow_) on_overflow_();
                continue;
            }
            std::lock_guard<std::mutex> lock(mutex_);
            const auto it = watches_.find(ev->wd);
            if (ev->mask & IN_IGNORED) {
                if (it != watches_.end()) watches_.erase(it);
            } else if (it != watches_.end() && ev->len > 0) {
                pending.push_back({it->second, ev->name, ev->mask});
            }
        }
    }
}

void FsWatcher::run() {
    std::vector<FsEvent> pending;
    while (!stop_) {
        pollfd fds[2] = {{inotify_fd_, POLLIN, 0}, {wake_fd_, POLLIN, 0}};
        const bool nothing_to_flush = pending.empty() || paused_;
        const int rc = ::poll(fds, 2, nothing_to_flush ? -1 : kDebounceMs);
        if (rc < 0) {
            if (errno == EINTR) continue;
            NEXUS_LOG_ERROR(std::string("File watcher stopped: poll failed: ") + std::strerror(errno));
            return;
        }
        if (stop_) return;
        if (fds[1].revents & POLLIN) {
            uint64_t counter;
            const ssize_t n = ::read(wake_fd_, &counter, sizeof counter);
            (void)n;
        }
        const bool got_events = (fds[0].revents & POLLIN) != 0;
        if (got_events) read_events(pending);

        // Deliver once the stream went quiet (or the batch is huge); held back while paused.
        if (pending.empty() || paused_ || (got_events && pending.size() < kMaxBatch)) continue;
        try {
            on_events_(pending);
        } catch (const std::exception& e) {
            NEXUS_LOG_ERROR(std::string("File watcher handler failed: ") + e.what());
        } catch (...) {
            NEXUS_LOG_ERROR("File watcher handler failed");
        }
        pending.clear();
    }
}

} // namespace nexus::utils
