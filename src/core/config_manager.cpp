#include "nexus/core/config_manager.hpp"
#include "nexus/utils/logger.hpp"
#include "nexus/utils/shortcut.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>

namespace nexus::core {

namespace {

std::string trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && (std::isspace(static_cast<unsigned char>(s[start])) || s[start] == '"' || s[start] == ',')) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && (std::isspace(static_cast<unsigned char>(s[end - 1])) || s[end - 1] == '"' || s[end - 1] == ',')) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

std::string expand_home(std::string_view path) {
    if (path.empty()) return "";
    if (path[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + std::string(path.substr(1));
        }
    }
    return std::string(path);
}

// Entries are written back without escaping (D-51), so reject what the writer cannot round-trip.
bool valid_exclusion(const std::string& name) {
    return !name.empty() && name.find_first_of("\"\\/") == std::string::npos;
}

} // namespace

ConfigManager& ConfigManager::instance() {
    static ConfigManager inst;
    return inst;
}

ConfigManager::ConfigManager() {
    config_file_path_ = resolve_config_path();
    set_defaults();
    load();
}

std::string ConfigManager::resolve_config_path() {
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path dir;
    if (xdg_config && *xdg_config) {
        dir = std::filesystem::path(xdg_config) / "nexus";
    } else {
        const char* home = std::getenv("HOME");
        dir = home ? std::filesystem::path(home) / ".config" / "nexus" : std::filesystem::path("/tmp/nexus");
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        // Fallback to local workspace config
        return ".nexus_config.json";
    }

    return (dir / "config.json").string();
}

std::vector<std::string> ConfigManager::default_file_exclusions() {
    return {"node_modules", "build", "target", "__pycache__", "venv", ".venv",
            "CMakeFiles", "Testing", "dist", "out"};
}

void ConfigManager::set_defaults() {
    file_exclusions_ = default_file_exclusions();
    enable_ctrl_np_navigation_ = true;
    shortcut_ = "Alt+Space";
    search_directories_.clear();

    const char* home = std::getenv("HOME");
    if (home) {
        std::filesystem::path home_path(home);
        std::error_code ec;

        std::vector<std::string> standard_dirs = {
            (home_path / "Documents").string(),
            (home_path / "Downloads").string(),
            (home_path / "Desktop").string(),
            (home_path / "Projects").string()
        };

        for (const auto& d : standard_dirs) {
            if (std::filesystem::exists(d, ec) && std::filesystem::is_directory(d, ec)) {
                search_directories_.push_back(d);
            }
        }

        // Add home itself as fallback if list is empty
        if (search_directories_.empty()) {
            search_directories_.push_back(home_path.string());
        }
    }
}

bool ConfigManager::load(const std::string& custom_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!custom_path.empty()) {
        config_file_path_ = custom_path;
    }

    std::ifstream file(config_file_path_);
    if (!file.is_open()) {
        // Create initial config file with defaults
        return false;
    }

    std::vector<std::string> loaded_dirs;
    std::string line;
    std::vector<std::string> loaded_exclusions;
    bool in_search_dirs = false;
    bool in_exclusions = false;
    bool have_exclusions = false;  // an explicit empty list means "exclude nothing"

    while (std::getline(file, line)) {
        if (line.find("\"enable_ctrl_np_navigation\"") != std::string::npos) {
            if (line.find("false") != std::string::npos) {
                enable_ctrl_np_navigation_ = false;
            } else if (line.find("true") != std::string::npos) {
                enable_ctrl_np_navigation_ = true;
            }
            continue;
        }

        if (line.find("\"global_shortcut\"") != std::string::npos) {
            size_t colon = line.find(':');
            std::string value = colon == std::string::npos ? "" : trim(line.substr(colon + 1));
            if (utils::parse_shortcut(value)) shortcut_ = value;
            continue;
        }

        if (line.find("\"file_exclusions\"") != std::string::npos) {
            have_exclusions = true;
            in_exclusions = line.find(']') == std::string::npos;
            continue;
        }

        if (in_exclusions) {
            if (line.find(']') != std::string::npos) {
                in_exclusions = false;
                continue;
            }
            std::string item = trim(line);
            if (valid_exclusion(item)) loaded_exclusions.push_back(item);
            continue;
        }

        if (line.find("\"search_directories\"") != std::string::npos) {
            in_search_dirs = true;
            continue;
        }

        if (in_search_dirs) {
            if (line.find(']') != std::string::npos) {
                in_search_dirs = false;
                continue;
            }

            std::string item = trim(line);
            if (!item.empty()) {
                loaded_dirs.push_back(expand_home(item));
            }
        }
    }

    if (!loaded_dirs.empty()) {
        search_directories_ = std::move(loaded_dirs);
    }
    if (have_exclusions) {
        file_exclusions_ = std::move(loaded_exclusions);
    }

    return true;
}

bool ConfigManager::save() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(config_file_path_);
    if (!file.is_open()) {
        // Try fallback to local directory
        config_file_path_ = ".nexus_config.json";
        file.open(config_file_path_);
        if (!file.is_open()) return false;
    }

    file << "{\n";
    file << "  \"enable_ctrl_np_navigation\": " << (enable_ctrl_np_navigation_ ? "true" : "false") << ",\n";
    file << "  \"global_shortcut\": \"" << shortcut_ << "\",\n";
    file << "  \"search_directories\": [\n";
    for (size_t i = 0; i < search_directories_.size(); ++i) {
        file << "    \"" << search_directories_[i] << "\"";
        if (i + 1 < search_directories_.size()) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ],\n";
    file << "  \"file_exclusions\": [\n";
    for (size_t i = 0; i < file_exclusions_.size(); ++i) {
        file << "    \"" << file_exclusions_[i] << "\"" << (i + 1 < file_exclusions_.size() ? "," : "") << "\n";
    }
    file << "  ]\n";
    file << "}\n";

    return true;
}

std::vector<std::string> ConfigManager::get_search_directories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return search_directories_;
}

bool ConfigManager::add_search_directory(const std::string& path) {
    std::string expanded = expand_home(path);
    std::error_code ec;
    if (!std::filesystem::exists(expanded, ec) || !std::filesystem::is_directory(expanded, ec)) {
        return false;
    }

    std::string canonical = std::filesystem::canonical(expanded, ec).string();
    if (canonical.empty()) canonical = expanded;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (std::find(search_directories_.begin(), search_directories_.end(), canonical) == search_directories_.end()) {
            search_directories_.push_back(canonical);
        }
    }
    return save();
}

bool ConfigManager::remove_search_directory(const std::string& path) {
    std::string expanded = expand_home(path);
    std::error_code ec;
    std::string canonical = std::filesystem::canonical(expanded, ec).string();
    if (canonical.empty()) canonical = expanded;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(search_directories_.begin(), search_directories_.end(), [&](const std::string& d) {
            return d == canonical || d == path || d == expanded;
        });
        if (it != search_directories_.end()) {
            search_directories_.erase(it, search_directories_.end());
        } else {
            return false;
        }
    }
    return save();
}

std::vector<std::string> ConfigManager::get_file_exclusions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return file_exclusions_;
}

bool ConfigManager::is_ctrl_np_navigation_enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return enable_ctrl_np_navigation_;
}

void ConfigManager::set_ctrl_np_navigation_enabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        enable_ctrl_np_navigation_ = enabled;
    }
    save();
}

std::string ConfigManager::get_shortcut() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shortcut_;
}

bool ConfigManager::set_shortcut(const std::string& shortcut) {
    if (!utils::parse_shortcut(shortcut)) return false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shortcut_ = shortcut;
    }
    return save();
}

} // namespace nexus::core
