#include "nexus/core/config_manager.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <cstdlib>
#include <unistd.h>

int main() {
    using namespace nexus::core;

    namespace fs = std::filesystem;
    const std::string pid = std::to_string(getpid());
    fs::path temp_config_file = fs::temp_directory_path() / ("nexus_test_config_" + pid + ".json");
    fs::remove(temp_config_file);

    // Test 0: defaults never include the process working directory (it would get indexed).
    // Defaults are built once in the singleton constructor, so chdir and redirect the config
    // dir (never the real ~/.config/nexus) before the first instance() call.
    const fs::path xdg = fs::temp_directory_path() / ("nexus_cfg_xdg_" + pid);
    const fs::path work = fs::temp_directory_path() / ("nexus_cfg_cwd_" + pid);
    const char* old_xdg_env = std::getenv("XDG_CONFIG_HOME");
    const std::optional<std::string> old_xdg = old_xdg_env ? std::optional<std::string>(old_xdg_env) : std::nullopt;
    const fs::path old_cwd = fs::current_path();
    fs::create_directories(xdg);
    fs::create_directories(work);
    setenv("XDG_CONFIG_HOME", xdg.c_str(), 1);
    fs::current_path(work);

    auto& config = ConfigManager::instance();
    const std::string cwd = fs::current_path().string();
    for (const auto& d : config.get_search_directories()) assert(d != cwd);

    fs::current_path(old_cwd);
    if (old_xdg) setenv("XDG_CONFIG_HOME", old_xdg->c_str(), 1); else unsetenv("XDG_CONFIG_HOME");
    fs::remove_all(work);
    fs::remove_all(xdg);

    // Test 1: Default state is true
    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == true);

    // Test 2: Disable Ctrl+N/P navigation and verify persistence
    config.set_ctrl_np_navigation_enabled(false);
    assert(config.is_ctrl_np_navigation_enabled() == false);

    // Reload from file to ensure it was written and parsed correctly
    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == false);

    // Test 3: Re-enable Ctrl+N/P navigation and verify persistence
    config.set_ctrl_np_navigation_enabled(true);
    assert(config.is_ctrl_np_navigation_enabled() == true);

    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == true);

    // Test 4: Global shortcut persists; invalid strings are rejected and leave it unchanged
    assert(config.set_shortcut("Ctrl+Shift+K"));
    assert(config.get_shortcut() == "Ctrl+Shift+K");
    assert(!config.set_shortcut("Space"));
    assert(config.get_shortcut() == "Ctrl+Shift+K");
    config.load(temp_config_file.string());
    assert(config.get_shortcut() == "Ctrl+Shift+K");
    assert(config.is_ctrl_np_navigation_enabled() == true);
    config.set_shortcut("Alt+Space");

    // Test 5: file exclusions default to the built-in list and survive a save/load round trip
    assert(config.get_file_exclusions() == ConfigManager::default_file_exclusions());
    assert(config.get_file_exclusions().size() == 10);
    config.set_ctrl_np_navigation_enabled(true);  // forces a save
    config.load(temp_config_file.string());
    assert(config.get_file_exclusions() == ConfigManager::default_file_exclusions());

    // Test 6: custom list is read; entries the writer could not round-trip (quote, backslash, slash) are dropped
    {
        std::ofstream(temp_config_file) << "{\n  \"file_exclusions\": [\n    \"vendor\",\n    \"bad\\\\name\",\n"
                                           "    \"a/b\",\n    \"x\"y\",\n    \"tmp\"\n  ]\n}\n";
    }
    config.load(temp_config_file.string());
    assert((config.get_file_exclusions() == std::vector<std::string>{"vendor", "tmp"}));

    // Test 7: an explicit empty list means "exclude nothing"
    std::ofstream(temp_config_file) << "{\n  \"file_exclusions\": []\n}\n";
    config.load(temp_config_file.string());
    assert(config.get_file_exclusions().empty());

    // Cleanup
    std::filesystem::remove(temp_config_file);

    std::cout << "All Config tests passed successfully!\n";
    return 0;
}

