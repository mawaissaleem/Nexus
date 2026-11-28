#include "nexus/core/config_manager.hpp"
#include "nexus/utils/logger.hpp"
#include "nexus/utils/json.hpp"
#include "nexus/utils/shortcut.hpp"
#include "nexus/utils/xdg.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cerrno>
#include <cstdlib>
#include <charconv>

namespace nexus::core {

namespace {

// Exclusions are bare entry names, never paths.
bool valid_exclusion(const std::string& name) {
    return !name.empty() && name.find_first_of("\"\\/") == std::string::npos;
}

}  // namespace

ConfigManager::ConfigManager(const std::string& config_path) : config_file_path_(config_path) {
    set_defaults();
    static_cast<void>(load());  // a missing file means defaults; a malformed one is logged inside load()
}

std::string ConfigManager::resolve_config_path() {
    const std::filesystem::path base = utils::xdg_user_dir("XDG_CONFIG_HOME", ".config");
    if (base.empty()) {
        NEXUS_LOG_WARN("No usable config directory (HOME and XDG_CONFIG_HOME unset or relative); "
                       "using default settings, settings not saved");
        return "";
    }

    const std::filesystem::path dir = base / "nexus";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        NEXUS_LOG_WARN("Cannot create " + dir.string() + ": " + ec.message() +
                       "; using default settings, settings not saved");
        return "";
    }

    return (dir / "config.json").string();
}

std::vector<std::string> ConfigManager::default_file_exclusions() {
    return {"node_modules", "build", "target", "__pycache__", "venv", ".venv", "CMakeFiles", "Testing", "dist", "out"};
}

void ConfigManager::set_defaults() {
    file_exclusions_ = default_file_exclusions();
    result_count_ = kDefaultResultCount;
    clipboard_retention_ = kDefaultClipboardRetention;
    disabled_providers_.clear();
    enable_ctrl_np_navigation_ = true;
    history_enabled_ = true;
    theme_ = utils::ThemeMode::Dark;
    shortcut_ = "Alt+Space";
    search_directories_.clear();

    const std::filesystem::path home_path = utils::home_dir();
    if (!home_path.empty()) {
        std::error_code ec;

        std::vector<std::string> standard_dirs = {(home_path / "Documents").string(),
                                                  (home_path / "Downloads").string(), (home_path / "Desktop").string(),
                                                  (home_path / "Projects").string()};

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

utils::Status ConfigManager::load(const std::string& custom_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!custom_path.empty()) {
        config_file_path_ = custom_path;
    }

    set_defaults();  // keys missing from the file must not keep values from a previous load

    std::ifstream file(config_file_path_);
    if (!file.is_open()) {
        return utils::Status::from_errno("can't read " + config_file_path_, errno);
    }
    std::stringstream buf;
    buf << file.rdbuf();

    utils::JsonValue root;
    std::string error;
    if (!utils::parse_json(buf.str(), root, &error) || !root.is_object()) {
        const std::string backup = utils::preserve_malformed_file(config_file_path_, buf.str());
        const std::string reason = error.empty() ? "top level is not an object" : error;
        const std::string kept_at = backup.empty() ? "nowhere (backup failed)" : backup;
        NEXUS_LOG_ERROR("Malformed config " + config_file_path_ + ": " + reason +
                        "; using defaults, original kept at " + kept_at);
        return utils::Status::error("can't use " + config_file_path_ + ": " + reason +
                                    " (using defaults, the original is kept at " + kept_at + ")");
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
    if (const auto* v = root.find("clipboard_retention"); v && v->type == utils::JsonValue::Type::Number) {
        int n = 0;
        const auto& t = v->string;  // raw number text, as for result_count
        const auto r = std::from_chars(t.data(), t.data() + t.size(), n);
        if (r.ec == std::errc() && r.ptr == t.data() + t.size()) {
            clipboard_retention_ = std::clamp(n, kMinClipboardRetention, kMaxClipboardRetention);
        } else if (r.ec == std::errc::result_out_of_range && !t.empty()) {
            clipboard_retention_ = t[0] == '-' ? kMinClipboardRetention : kMaxClipboardRetention;
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
            if (!item.is_string() || item.string.empty()) continue;
            const std::optional<std::string> expanded = utils::expand_home(item.string);
            if (expanded) {
                dirs.push_back(*expanded);
            } else {
                NEXUS_LOG_WARN("Skipping search directory " + item.string + ": HOME is unset or not absolute");
            }
        }
        if (!dirs.empty()) search_directories_ = std::move(dirs);
    }

    return {};
}

utils::Status ConfigManager::save() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (config_file_path_.empty()) {
        NEXUS_LOG_ERROR("No config directory; settings not saved");
        return utils::Status::error("can't save settings: there is no usable config directory");
    }
    std::ofstream file(config_file_path_);
    if (!file.is_open()) {
        const utils::Status status = utils::Status::from_errno("can't write " + config_file_path_, errno);
        NEXUS_LOG_ERROR(status.message());
        return status;
    }

    file << "{\n";
    file << "  \"enable_ctrl_np_navigation\": " << (enable_ctrl_np_navigation_ ? "true" : "false") << ",\n";
    file << "  \"history_enabled\": " << (history_enabled_ ? "true" : "false") << ",\n";
    file << "  \"theme\": " << utils::json_quote(utils::theme_mode_name(theme_)) << ",\n";
    file << "  \"global_shortcut\": " << utils::json_quote(shortcut_) << ",\n";
    file << "  \"result_count\": " << result_count_ << ",\n";
    file << "  \"clipboard_retention\": " << clipboard_retention_ << ",\n";
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
        file << "    " << utils::json_quote(file_exclusions_[i]) << (i + 1 < file_exclusions_.size() ? "," : "")
             << "\n";
    }
    file << "  ]\n";
    file << "}\n";

    // Close now so a write that failed late (disk full) is caught here and not lost in the destructor.
    file.close();
    if (file.fail()) {
        const utils::Status status = utils::Status::from_errno("can't write " + config_file_path_, errno);
        NEXUS_LOG_ERROR(status.message());
        return status;
    }
    return {};
}

std::vector<std::string> ConfigManager::get_search_directories() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return search_directories_;
}

utils::Status ConfigManager::add_search_directory(const std::string& path) {
    const std::optional<std::string> home_expanded = utils::expand_home(path);
    if (!home_expanded) return utils::Status::error("can't add " + path + ": HOME is unset or not absolute");

    const std::string& expanded = *home_expanded;
    std::error_code ec;
    // `exists` is also false when the path can't be checked (EACCES); then `ec` holds the real reason.
    const bool found = std::filesystem::exists(expanded, ec);
    if (ec) return utils::Status::error("can't add " + path + ": " + ec.message());
    if (!found) return utils::Status::error("can't add " + path + ": no such directory");
    const bool is_dir = std::filesystem::is_directory(expanded, ec);
    if (ec) return utils::Status::error("can't add " + path + ": " + ec.message());
    if (!is_dir) return utils::Status::error("can't add " + path + ": not a directory");

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

utils::Status ConfigManager::remove_search_directory(const std::string& path) {
    const std::string expanded = utils::expand_home(path).value_or(path);
    std::error_code ec;
    std::string canonical = std::filesystem::canonical(expanded, ec).string();
    if (canonical.empty()) canonical = expanded;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(search_directories_.begin(), search_directories_.end(),
                                 [&](const std::string& d) { return d == canonical || d == path || d == expanded; });
        if (it != search_directories_.end()) {
            search_directories_.erase(it, search_directories_.end());
        } else {
            return utils::Status::error(path + " is not in the search directory list");
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
            if (valid_exclusion(n) &&
                std::find(file_exclusions_.begin(), file_exclusions_.end(), n) == file_exclusions_.end()) {
                file_exclusions_.push_back(n);
            }
        }
    }
    static_cast<void>(save());  // these setters can't report; save() logs the failure
}

int ConfigManager::get_result_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return result_count_;
}

int ConfigManager::get_clipboard_retention() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return clipboard_retention_;
}

void ConfigManager::set_result_count(int count) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        result_count_ = std::clamp(count, kMinResultCount, kMaxResultCount);
    }
    static_cast<void>(save());  // these setters can't report; save() logs the failure
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
    static_cast<void>(save());  // these setters can't report; save() logs the failure
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
    static_cast<void>(save());  // these setters can't report; save() logs the failure
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
    static_cast<void>(save());  // these setters can't report; save() logs the failure
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
    static_cast<void>(save());  // these setters can't report; save() logs the failure
}

std::string ConfigManager::get_shortcut() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shortcut_;
}

utils::Status ConfigManager::set_shortcut(const std::string& shortcut) {
    if (!utils::parse_shortcut(shortcut)) {
        const std::string hint = "use modifiers plus a key, like Alt+Space or Ctrl+Shift+K";
        return utils::Status::error("\"" + shortcut + "\" is not a valid shortcut: " + hint);
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shortcut_ = shortcut;
    }
    return save();
}

}  // namespace nexus::core
