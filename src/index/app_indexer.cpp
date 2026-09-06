#include "nexus/index/app_indexer.hpp"
#include "nexus/utils/logger.hpp"
#include <unordered_set>
#include <cstdlib>
#include <sstream>

namespace nexus::index {

AppIndexer::AppIndexer(std::shared_ptr<Database> db)
    : db_(std::move(db)) {}

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
    std::vector<DesktopEntry> new_apps;
    std::unordered_set<std::string> seen_ids;

    auto dirs = get_default_app_directories();
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
        applications_ = std::move(new_apps);
    }

    return applications_.size();
}

size_t AppIndexer::initialize() {
    if (db_ && db_->is_open()) {
        auto cached = db_->load_applications();
        if (!cached.empty()) {
            NEXUS_LOG_INFO("Loaded " + std::to_string(cached.size()) + " applications from database cache.");
            std::lock_guard<std::mutex> lock(mutex_);
            applications_ = std::move(cached);
            return applications_.size();
        }
    }

    return rebuild_index();
}

std::vector<DesktopEntry> AppIndexer::get_applications() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return applications_;
}

} // namespace nexus::index

