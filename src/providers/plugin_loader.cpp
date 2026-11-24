#include "nexus/providers/plugin_loader.hpp"
#include "nexus/utils/logger.hpp"
#include "nexus/utils/xdg.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>

namespace nexus::providers {

namespace fs = std::filesystem;

// Programs launched from the AppImage inherit $APPDIR, and once the image unmounts another user could recreate
// that path. So it only counts when this very binary runs from inside it.
static fs::path appimage_plugin_dir() {
    const char* appdir = std::getenv("APPDIR");
    if (!appdir || !fs::path(appdir).is_absolute()) return {};

    std::error_code exe_error;
    std::error_code root_error;
    const fs::path exe = fs::canonical("/proc/self/exe", exe_error);
    const fs::path root = fs::canonical(appdir, root_error);
    if (exe_error || root_error) return {};

    const bool exe_inside_root = exe.native().starts_with(root.native() + "/");
    if (!exe_inside_root) return {};
    return root / "usr" / "share" / "nexus" / "plugins";
}

std::vector<fs::path> plugin_search_dirs() {
    std::vector<fs::path> dirs;
    const fs::path data_home = utils::xdg_user_dir("XDG_DATA_HOME", fs::path(".local") / "share");
    if (!data_home.empty()) dirs.push_back(data_home / "nexus" / "plugins");

    const char* xdg = std::getenv("XDG_DATA_DIRS");
    std::stringstream ss((xdg && *xdg) ? xdg : "/usr/local/share:/usr/share");
    for (std::string entry; std::getline(ss, entry, ':');) {
        // Relative entries are invalid per the XDG spec and would run plugins from the cwd.
        if (fs::path(entry).is_absolute()) dirs.push_back(fs::path(entry) / "nexus" / "plugins");
    }

    // Inside an AppImage the plugins sit under $APPDIR, which is not on XDG_DATA_DIRS. Last, so installed copies win.
    const fs::path appimage_plugins = appimage_plugin_dir();
    if (!appimage_plugins.empty()) dirs.push_back(appimage_plugins);
    return dirs;
}

std::vector<plugin::Manifest> discover_plugins(const std::vector<fs::path>& roots,
                                               const std::set<std::string>& reserved) {
    std::vector<plugin::Manifest> found;
    std::set<std::string> seen;
    for (const auto& root : roots) {
        std::error_code ec;
        std::vector<fs::path> dirs;
        for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_directory(ec)) dirs.push_back(it->path());
        }
        std::sort(dirs.begin(), dirs.end());  // same order every run
        for (const auto& dir : dirs) {
            std::string err;
            auto manifest = plugin::parse_manifest(dir, &err);
            if (!manifest) {
                NEXUS_LOG_WARN("Plugin " + dir.string() + " skipped: " + err);
            } else if (reserved.count(manifest->id)) {
                NEXUS_LOG_WARN("Plugin " + dir.string() + " skipped: id '" + manifest->id + "' is reserved");
            } else if (!seen.insert(manifest->id).second) {
                NEXUS_LOG_INFO("Plugin " + dir.string() + " skipped: id '" + manifest->id +
                               "' already loaded from an earlier directory");
            } else {
                found.push_back(std::move(*manifest));
            }
        }
    }
    return found;
}

}  // namespace nexus::providers
