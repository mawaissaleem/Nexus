#pragma once

#include "nexus/index/desktop_entry.hpp"
#include "nexus/index/database.hpp"
#include <vector>
#include <string>
#include <memory>
#include <mutex>
#include <filesystem>
#include <optional>
#include "nexus/utils/fs_watcher.hpp"

namespace nexus::index {

class AppIndexer {
public:
    // `directories` replaces the default XDG/snap/flatpak list (tests, hermetic runs); listed order = priority.
    explicit AppIndexer(std::shared_ptr<Database> db = nullptr,
                        std::optional<std::vector<std::filesystem::path>> directories = std::nullopt);
    ~AppIndexer();

    // Rebuild index by scanning filesystem locations and storing to database. Thread-safe; concurrent
    // calls (tray rebuild vs. watcher) run one after the other, so the later scan always lands last.
    size_t rebuild_index();

    // Keeps the index current: a .desktop file added/removed/renamed in any app directory (or its subdirectories)
    // triggers a debounced full rebuild_index() on the watcher thread. Directories that do not exist yet are
    // only picked up by the next rebuild/restart. Returns false if the OS refuses to watch. Call from one thread.
    bool start_watching();
    // Joins the watcher thread (any rebuild it is running finishes first). Safe to call twice.
    void stop_watching();

    // Load applications (from database if available and non-empty, otherwise scans)
    size_t initialize();

    // Get current indexed applications
    // Immutable snapshot: stays valid even if rebuild_index() swaps the list afterwards. Never null.
    [[nodiscard]] std::shared_ptr<const std::vector<DesktopEntry>> get_applications() const;

    // Standard application directory search paths
    static std::vector<std::filesystem::path> get_default_app_directories();

private:
    [[nodiscard]] std::vector<std::filesystem::path> directories() const;
    void watch_directories();

    std::shared_ptr<Database> db_;
    std::optional<std::vector<std::filesystem::path>> directories_;
    std::mutex rebuild_mutex_;  // serialises rebuild_index() (scan + save + swap)
    std::unique_ptr<utils::FsWatcher> watcher_;
    std::shared_ptr<const std::vector<DesktopEntry>> applications_ = std::make_shared<const std::vector<DesktopEntry>>();
    mutable std::mutex mutex_;
};

} // namespace nexus::index

