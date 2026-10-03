#pragma once

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

    bool is_ctrl_np_navigation_enabled() const;
    void set_ctrl_np_navigation_enabled(bool enabled);

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
    bool enable_ctrl_np_navigation_{true};
    std::string shortcut_{"Alt+Space"};
};

} // namespace nexus::core
