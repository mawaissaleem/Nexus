#include "nexus/core/config_manager.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>

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

    // Cleanup
    std::filesystem::remove(temp_config_file);

    std::cout << "All Config tests passed successfully!\n";
    return 0;
}

