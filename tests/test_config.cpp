#include "nexus/core/config_manager.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>

int main() {
    using namespace nexus::core;

    std::filesystem::path temp_config_file = std::filesystem::temp_directory_path() / "nexus_test_config.json";
    std::filesystem::remove(temp_config_file);

    auto& config = ConfigManager::instance();
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

