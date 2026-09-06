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

    std::string get_config_path() const { return config_file_path_; }

private:
    ConfigManager();
    ~ConfigManager() = default;

    void set_defaults();
    static std::string resolve_config_path();

    mutable std::mutex mutex_;
    std::string config_file_path_;
    std::vector<std::string> search_directories_;
};

} // namespace nexus::core
