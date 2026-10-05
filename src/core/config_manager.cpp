#include "nexus/core/config_manager.hpp"
#include "nexus/utils/logger.hpp"
#include "nexus/utils/json.hpp"
#include "nexus/utils/shortcut.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cstdlib>
#include <charconv>

namespace nexus::core {

namespace {

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

// Exclusions are bare entry names, never paths.
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
    result_count_ = kDefaultResultCount;
    disabled_providers_.clear();
    enable_ctrl_np_navigation_ = true;
    history_enabled_ = true;
    theme_ = utils::ThemeMode::Dark;
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

    set_defaults();  // keys missing from the file must not keep values from a previous load (D-60)

    std::ifstream file(config_file_path_);
    if (!file.is_open()) {
        return false;
    }
    std::stringstream buf;
    buf << file.rdbuf();

    utils::JsonValue root;
    std::string error;
    if (!utils::parse_json(buf.str(), root, &error) || !root.is_object()) {
        const std::string backup = utils::preserve_malformed_file(config_file_path_, buf.str());
        NEXUS_LOG_ERROR("Malformed config " + config_file_path_ + ": " +
                        (error.empty() ? "top level is not an object" : error) + "; using defaults, original kept at " +
                        (backup.empty() ? "nowhere (backup failed)" : backup));
        return false;
    }

    if (const auto* v = root.find("enable_ctrl_np_navigation"); v && v->is_bool()) {
        enable_ctrl_np_navigation_ = v->boolean;
    }
    if (const auto* v = root.find("history_enabled"); v && v->is_bool()) {
        history_enabled_ = v->boolean;
    }
    if (const auto* v = root.find("theme"); v && v->is_string()) {
        if (const auto mode = utils::parse_theme_mode(v->string)) theme_ = *mode;
    }
    if (const auto* v = root.find("global_shortcut"); v && v->is_string() && utils::parse_shortcut(v->string)) {
        shortcut_ = v->string;
    }
    if (const auto* v = root.find("file_exclusions"); v && v->is_array()) {  // an empty list means "exclude nothing"
        file_exclusions_.clear();
        for (const auto& item : v->array) {
            if (item.is_string() && valid_exclusion(item.string)) file_exclusions_.push_back(item.string);
        }
    }
    if (const auto* v = root.find("result_count"); v && v->type == utils::JsonValue::Type::Number) {
        int n = 0;
        const auto& t = v->string;  // raw number text; "7.5" or "1e1" are not integers -> default
        const auto r = std::from_chars(t.data(), t.data() + t.size(), n);
        if (r.ec == std::errc() && r.ptr == t.data() + t.size()) {
            result_count_ = std::clamp(n, kMinResultCount, kMaxResultCount);
        } else if (r.ec == std::errc::result_out_of_range && !t.empty()) {
            result_count_ = t[0] == '-' ? kMinResultCount : kMaxResultCount;
        }
    }
    if (const auto* v = root.find("disabled_providers"); v && v->is_array()) {
        for (const auto& item : v->array) {
            if (item.is_string() && !item.string.empty()) disabled_providers_.push_back(item.string);
        }
    }
    if (const auto* v = root.find("search_directories"); v && v->is_array()) {
        std::vector<std::string> dirs;
        for (const auto& item : v->array) {
            if (item.is_string() && !item.string.empty()) dirs.push_back(expand_home(item.string));
        }
        if (!dirs.empty()) search_directories_ = std::move(dirs);
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
    file << "  \"history_enabled\": " << (history_enabled_ ? "true" : "false") << ",\n";
    file << "  \"theme\": " << utils::json_quote(utils::theme_mode_name(theme_)) << ",\n";
    file << "  \"global_shortcut\": " << utils::json_quote(shortcut_) << ",\n";
    file << "  \"result_count\": " << result_count_ << ",\n";
    file << "  \"disabled_providers\": [";
    for (size_t i = 0; i < disabled_providers_.size(); ++i) {
        file << (i ? ", " : "") << utils::json_quote(disabled_providers_[i]);
    }
    file << "],\n";
    file << "  \"search_directories\": [\n";
    for (size_t i = 0; i < search_directories_.size(); ++i) {
        file << "    " << utils::json_quote(search_directories_[i]);
        if (i + 1 < search_directories_.size()) {
            file << ",";
        }
        file << "\n";
    }
    file << "  ],\n";
    file << "  \"file_exclusions\": [\n";
    for (size_t i = 0; i < file_exclusions_.size(); ++i) {
        file << "    " << utils::json_quote(file_exclusions_[i]) << (i + 1 < file_exclusions_.size() ? "," : "") << "\n";
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

void ConfigManager::set_file_exclusions(const std::vector<std::string>& names) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        file_exclusions_.clear();
        for (const auto& n : names) {
            if (valid_exclusion(n) && std::find(file_exclusions_.begin(), file_exclusions_.end(), n) == file_exclusions_.end()) {
                file_exclusions_.push_back(n);
            }
        }
    }
    save();
}

int ConfigManager::get_result_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return result_count_;
}

void ConfigManager::set_result_count(int count) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_count_ = std::clamp(count, kMinResultCount, kMaxResultCount);
    }
    save();
}

bool ConfigManager::is_provider_enabled(const std::string& provider_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::find(disabled_providers_.begin(), disabled_providers_.end(), provider_id) == disabled_providers_.end();
}

void ConfigManager::set_provider_enabled(const std::string& provider_id, bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = std::find(disabled_providers_.begin(), disabled_providers_.end(), provider_id);
        if (enabled && it != disabled_providers_.end()) disabled_providers_.erase(it);
        if (!enabled && it == disabled_providers_.end()) disabled_providers_.push_back(provider_id);
    }
    save();
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

bool ConfigManager::is_history_enabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return history_enabled_;
}

void ConfigManager::set_history_enabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        history_enabled_ = enabled;
    }
    save();
}

utils::ThemeMode ConfigManager::get_theme() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return theme_;
}

void ConfigManager::set_theme(utils::ThemeMode mode) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        theme_ = mode;
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
