#include "nexus/utils/xdg.hpp"

#include <cstdlib>

namespace nexus::utils {

std::filesystem::path home_dir() {
    const char* home = std::getenv("HOME");
    if (home && std::filesystem::path(home).is_absolute()) return home;
    return {};
}

std::filesystem::path xdg_user_dir(const char* xdg_var, const std::filesystem::path& home_subdir) {
    const char* xdg = std::getenv(xdg_var);
    if (xdg && std::filesystem::path(xdg).is_absolute()) return xdg;

    const std::filesystem::path home = home_dir();
    if (home.empty()) return {};
    return home / home_subdir;
}

std::optional<std::string> expand_home(std::string_view path) {
    const bool is_home_path = path == "~" || path.substr(0, 2) == "~/";
    if (!is_home_path) return std::string(path);

    const std::filesystem::path home = home_dir();
    if (home.empty()) return std::nullopt;
    return home.string() + std::string(path.substr(1));
}

}  // namespace nexus::utils
