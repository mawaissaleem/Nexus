#include "nexus/index/app_indexer.hpp"
#include "nexus/utils/logger.hpp"
#include <unordered_set>
#include <cstdlib>
#include <sstream>
#include <sys/inotify.h>

namespace nexus::index {

AppIndexer::AppIndexer(std::shared_ptr<Database> db, std::optional<std::vector<std::filesystem::path>> directories)
    : db_(std::move(db)), directories_(std::move(directories)) {}

AppIndexer::~AppIndexer() { stop_watching(); }

std::vector<std::filesystem::path> AppIndexer::directories() const {
    return directories_ ? *directories_ : get_default_app_directories();
}

std::vector<std::filesystem::path> AppIndexer::get_default_app_directories() {
    std::vector<std::filesystem::path> dirs;
    std::unordered_set<std::string> seen;

    auto add_dir = [&](const std::filesystem::path& p) {
        std::error_code ec;
        if (std::filesystem::exists(p, ec) && std::filesystem::is_directory(p, ec)) {
            std::string canonical = std::filesystem::canonical(p, ec).string();
            if (!canonical.empty() && seen.insert(canonical).second) {
                dirs.push_back(p);
            }
        }
    };

    // 1. User local applications
    const char* xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home && *xdg_data_home) {
        add_dir(std::filesystem::path(xdg_data_home) / "applications");
    } else {
        const char* home = std::getenv("HOME");
        if (home && *home) {
            add_dir(std::filesystem::path(home) / ".local" / "share" / "applications");
        }
    }

    // 2. XDG_DATA_DIRS
    const char* xdg_data_dirs = std::getenv("XDG_DATA_DIRS");
    if (xdg_data_dirs && *xdg_data_dirs) {
        std::istringstream ss(xdg_data_dirs);
        std::string token;
        while (std::getline(ss, token, ':')) {
            if (!token.empty()) {
                add_dir(std::filesystem::path(token) / "applications");
            }
        }
    } else {
        // Fallbacks
        add_dir("/usr/local/share/applications");
        add_dir("/usr/share/applications");
    }

    // Snap and Flatpak applications
    add_dir("/var/lib/snapd/desktop/applications");
    add_dir("/var/lib/flatpak/exports/share/applications");

    const char* home = std::getenv("HOME");
    if (home && *home) {
        add_dir(std::filesystem::path(home) / ".local" / "share" / "flatpak" / "exports" / "share" / "applications");
    }

    return dirs;
}

size_t AppIndexer::rebuild_index() {
    std::lock_guard<std::mutex> rebuild_lock(rebuild_mutex_);
    std::vector<DesktopEntry> new_apps;
    std::unordered_set<std::string> seen_ids;

    auto dirs = directories();
    NEXUS_LOG_INFO("Scanning " + std::to_string(dirs.size()) + " application directories...");

    for (const auto& dir : dirs) {
        std::error_code ec;
        for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, std::filesystem::directory_options::skip_permission_denied, ec)) {
            if (ec) continue;
            if (entry.is_regular_file(ec) && entry.path().extension() == ".desktop") {
                std::string id = entry.path().stem().string();
                if (seen_ids.count(id)) {
                    continue; // earlier directories (e.g. user overrides) have priority
                }

                auto parsed = DesktopEntry::parse(entry.path());
                if (parsed && parsed->is_valid_application()) {
                    seen_ids.insert(id);
                    new_apps.push_back(std::move(*parsed));
                }
            }
        }
    }

    NEXUS_LOG_INFO("Discovered " + std::to_string(new_apps.size()) + " applications.");

    if (db_ && db_->is_open()) {
        db_->save_applications(new_apps);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        applications_ = std::make_shared<const std::vector<DesktopEntry>>(std::move(new_apps));
        return applications_->size();
    }
}

// Watches every app directory and all directories below it (rebuild_index() scans recursively, and
// applications/kde/foo.desktop has id kde-foo). Failed/limit-hit watches just mean slower pickup via rebuild.
void AppIndexer::watch_directories() {
    for (const auto& configured : directories()) {
        std::error_code ec;
        // FsWatcher never follows symlinks, but the scan does: watch the real directory (dotfiles-linked app dirs).
        auto dir = std::filesystem::canonical(configured, ec);
        if (ec) dir = configured;
        ec.clear();
        if (watcher_->add_watch(dir.string()) != utils::FsWatcher::AddResult::Ok) continue;
        for (std::filesystem::recursive_directory_iterator it(dir, std::filesystem::directory_options::skip_permission_denied, ec), end;
             !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec) && !it->is_symlink(ec)) watcher_->add_watch(it->path().string());
        }
    }
}

bool AppIndexer::start_watching() {
    stop_watching();
    auto rebuild = [this]() {
        // Watch first, scan second: a directory created while we scan raises another event instead of being missed.
        watch_directories();
        rebuild_index();
    };
    watcher_ = std::make_unique<utils::FsWatcher>(
        [rebuild](const std::vector<utils::FsEvent>& events) {
            for (const auto& ev : events) {
                // Installers write via rename (IN_MOVED_TO); directory events cover moved-in/new subdirectories.
                if ((ev.mask & IN_ISDIR) || (ev.name.size() > 8 && ev.name.compare(ev.name.size() - 8, 8, ".desktop") == 0)) {
                    rebuild();
                    return;
                }
            }
        },
        rebuild);
    if (!watcher_->start(false)) {
        watcher_.reset();
        return false;
    }
    watch_directories();
    return true;
}

void AppIndexer::stop_watching() {
    if (watcher_) watcher_->stop();  // join before release: the thread calls back into this object
    watcher_.reset();
}

size_t AppIndexer::initialize() {
    if (db_ && db_->is_open()) {
        auto cached = db_->load_applications();
        if (!cached.empty()) {
            NEXUS_LOG_INFO("Loaded " + std::to_string(cached.size()) + " applications from database cache.");
            std::lock_guard<std::mutex> lock(mutex_);
            applications_ = std::make_shared<const std::vector<DesktopEntry>>(std::move(cached));
            return applications_->size();
        }
    }

    return rebuild_index();
}

std::shared_ptr<const std::vector<DesktopEntry>> AppIndexer::get_applications() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return applications_;
}

} // namespace nexus::index

