#pragma once

#include "nexus/utils/theme.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <filesystem>

namespace nexus::core {

class ConfigManager {
public:
    static ConfigManager& instance();

    bool load(const std::string& custom_path = "");
    bool save();

    std::vector<std::string> get_search_directories() const;
    bool add_search_directory(const std::string& path);
    bool remove_search_directory(const std::string& path);

    // Names (not paths) the file indexer skips. Edited by hand in config.json ("file_exclusions").
    std::vector<std::string> get_file_exclusions() const;
    static std::vector<std::string> default_file_exclusions();
    // Replaces the list (entries that are not bare names, and duplicates, are dropped) and saves.
    void set_file_exclusions(const std::vector<std::string>& names);

    // "result_count": how many results the launcher shows. Default 7, clamped to [min, max].
    static constexpr int kMinResultCount = 1;
    static constexpr int kMaxResultCount = 15;
    static constexpr int kDefaultResultCount = 7;
    int get_result_count() const;
    void set_result_count(int count);

    // "disabled_providers": ids of providers switched off (default none). Unknown ids are kept.
    bool is_provider_enabled(const std::string& provider_id) const;
    void set_provider_enabled(const std::string& provider_id, bool enabled);

    bool is_ctrl_np_navigation_enabled() const;
    void set_ctrl_np_navigation_enabled(bool enabled);

    // Usage and query history recording (default on). Turning it off does not delete existing history.
    bool is_history_enabled() const;
    void set_history_enabled(bool enabled);

    // "theme" in config.json: dark (default), light or system. Unknown values fall back to dark.
    utils::ThemeMode get_theme() const;
    void set_theme(utils::ThemeMode mode);

    std::string get_shortcut() const;
    // Returns false (and changes nothing) if `shortcut` does not parse.
    bool set_shortcut(const std::string& shortcut);

    std::string get_config_path() const { return config_file_path_; }

private:
    ConfigManager();
    ~ConfigManager() = default;

    void set_defaults();
    static std::string resolve_config_path();

    mutable std::mutex mutex_;
    std::string config_file_path_;
    std::vector<std::string> search_directories_;
    std::vector<std::string> file_exclusions_;
    int result_count_{kDefaultResultCount};
    std::vector<std::string> disabled_providers_;
    bool enable_ctrl_np_navigation_{true};
    bool history_enabled_{true};
    utils::ThemeMode theme_{utils::ThemeMode::Dark};
    std::string shortcut_{"Alt+Space"};
};

} // namespace nexus::core
