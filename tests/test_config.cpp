#include "nexus/core/config_manager.hpp"
#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <iterator>
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

    // Test 6: custom list is read; names that are paths or contain quote/backslash are dropped
    {
        std::ofstream(temp_config_file) << "{\n  \"file_exclusions\": [\n    \"vendor\",\n    \"bad\\\\name\",\n"
                                           "    \"a/b\",\n    \"x\\\"y\",\n    \"tmp\"\n  ]\n}\n";
    }
    config.load(temp_config_file.string());
    assert((config.get_file_exclusions() == std::vector<std::string>{"vendor", "tmp"}));

    // Test 7: an explicit empty list means "exclude nothing"
    std::ofstream(temp_config_file) << "{\n  \"file_exclusions\": []\n}\n";
    config.load(temp_config_file.string());
    assert(config.get_file_exclusions().empty());

    // Test 8: malformed JSON logs an error and falls back to defaults (nothing from the file leaks in)
    std::ofstream(temp_config_file) << "{\n  \"enable_ctrl_np_navigation\": false,\n  \"global_shortcut\": \"Ctrl+K\"\n";  // unterminated
    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == true);
    assert(config.get_shortcut() == "Alt+Space");
    assert(config.get_file_exclusions() == ConfigManager::default_file_exclusions());
    std::ofstream(temp_config_file) << "[1, 2]";  // valid JSON, wrong shape
    assert(config.load(temp_config_file.string()) == false);

    // Test 9 (D-64): one-line arrays parse
    std::ofstream(temp_config_file) << "{\"file_exclusions\": [\"a\", \"b\"], \"search_directories\": [\"/tmp\"]}";
    assert(config.load(temp_config_file.string()));
    assert((config.get_file_exclusions() == std::vector<std::string>{"a", "b"}));
    assert((config.get_search_directories() == std::vector<std::string>{"/tmp"}));

    // Test 10 (D-60): keys absent from the file reset to defaults instead of keeping the last load
    std::ofstream(temp_config_file) << "{\"enable_ctrl_np_navigation\": false}";
    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == false);
    std::ofstream(temp_config_file) << "{}";
    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == true);
    assert(config.get_file_exclusions() == ConfigManager::default_file_exclusions());

    // Test 11 (D-51): directories with quote/backslash round-trip through save/load
    const fs::path odd = fs::temp_directory_path() / ("nexus_cfg_odd\\\"dir_" + pid);
    fs::create_directories(odd);
    assert(config.add_search_directory(odd.string()));
    config.load(temp_config_file.string());
    auto dirs = config.get_search_directories();
    assert(std::find(dirs.begin(), dirs.end(), fs::canonical(odd).string()) != dirs.end());
    fs::remove_all(odd);

    // Test 12: a file in the old writer's format (unescaped, one item per line) still loads
    std::ofstream(temp_config_file) << "{\n  \"enable_ctrl_np_navigation\": false,\n  \"global_shortcut\": \"Ctrl+Shift+K\",\n"
                                       "  \"search_directories\": [\n    \"/tmp\",\n    \"/usr\"\n  ],\n"
                                       "  \"file_exclusions\": [\n    \"node_modules\"\n  ]\n}\n";
    assert(config.load(temp_config_file.string()));
    assert(!config.is_ctrl_np_navigation_enabled());
    assert(config.get_shortcut() == "Ctrl+Shift+K");
    assert((config.get_search_directories() == std::vector<std::string>{"/tmp", "/usr"}));
    assert((config.get_file_exclusions() == std::vector<std::string>{"node_modules"}));

    // Test 13: a 0.3.0 file with an unescaped backslash path still loads (lenient unknown escape)
    std::ofstream(temp_config_file) << "{\n  \"enable_ctrl_np_navigation\": false,\n  \"global_shortcut\": \"Ctrl+Shift+K\",\n"
                                       "  \"search_directories\": [\n    \"/srv/my\\dir\",\n    \"/srv/data\"\n  ],\n"
                                       "  \"file_exclusions\": [\n    \"foo\"\n  ]\n}\n";
    assert(config.load(temp_config_file.string()));
    assert(!config.is_ctrl_np_navigation_enabled());
    assert((config.get_search_directories() == std::vector<std::string>{"/srv/my\\dir", "/srv/data"}));

    // Test 14: a file that still cannot be parsed is kept aside before save() overwrites it
    const std::string broken = "{\"global_shortcut\": \"Ctrl+Shift+K\", \"search_directories\": [\"/srv/data\"";
    const fs::path backup = temp_config_file.string() + ".malformed";
    fs::remove(backup);
    std::ofstream(temp_config_file) << broken;
    assert(!config.load(temp_config_file.string()));
    assert(config.add_search_directory(fs::temp_directory_path().string()));  // save() rewrites the file
    {
        std::ifstream in(backup);
        assert(std::string(std::istreambuf_iterator<char>(in), {}) == broken);
    }
    fs::remove(backup);

    // Test 15: history_enabled defaults to on when absent (old config), and round-trips through save()/load()
    std::ofstream(temp_config_file) << "{\n  \"global_shortcut\": \"Alt+Space\"\n}\n";
    assert(config.load(temp_config_file.string()));
    assert(config.is_history_enabled());
    config.set_history_enabled(false);  // saves
    assert(config.load(temp_config_file.string()));
    assert(!config.is_history_enabled());
    config.set_history_enabled(true);
    assert(config.load(temp_config_file.string()) && config.is_history_enabled());
    std::ofstream(temp_config_file) << "{\"history_enabled\": false}";
    assert(config.load(temp_config_file.string()) && !config.is_history_enabled());
    std::ofstream(temp_config_file) << "{\"history_enabled\": \"no\"}";  // wrong type -> default
    assert(config.load(temp_config_file.string()) && config.is_history_enabled());

    // Test 16: theme defaults to dark (old config), round-trips, and falls back to dark on bad values
    using nexus::utils::ThemeMode;
    std::ofstream(temp_config_file) << "{\n  \"global_shortcut\": \"Alt+Space\"\n}\n";
    assert(config.load(temp_config_file.string()) && config.get_theme() == ThemeMode::Dark);
    config.set_theme(ThemeMode::System);  // saves
    assert(config.load(temp_config_file.string()) && config.get_theme() == ThemeMode::System);
    config.set_theme(ThemeMode::Light);
    assert(config.load(temp_config_file.string()) && config.get_theme() == ThemeMode::Light);
    std::ofstream(temp_config_file) << "{\"theme\": \"solarized\"}";  // unknown name -> default
    assert(config.load(temp_config_file.string()) && config.get_theme() == ThemeMode::Dark);
    std::ofstream(temp_config_file) << "{\"theme\": 3}";  // wrong type -> default
    assert(config.load(temp_config_file.string()) && config.get_theme() == ThemeMode::Dark);
    std::ofstream(temp_config_file) << "{\"theme\": \"light\"}";
    assert(config.load(temp_config_file.string()) && config.get_theme() == ThemeMode::Light);
    std::ofstream(temp_config_file) << "{}";  // absent after a light load -> back to default, not stale
    assert(config.load(temp_config_file.string()) && config.get_theme() == ThemeMode::Dark);

    // Test 17 (P8.6): result_count default, round trip, clamping, invalid values
    std::ofstream(temp_config_file) << "{}";
    assert(config.load(temp_config_file.string()) && config.get_result_count() == 7);
    config.set_result_count(12);  // saves
    assert(config.load(temp_config_file.string()) && config.get_result_count() == 12);
    config.set_result_count(500);
    assert(config.get_result_count() == ConfigManager::kMaxResultCount);
    config.set_result_count(0);
    assert(config.get_result_count() == ConfigManager::kMinResultCount);
    std::ofstream(temp_config_file) << "{\"result_count\": 99}";
    assert(config.load(temp_config_file.string()) && config.get_result_count() == ConfigManager::kMaxResultCount);
    std::ofstream(temp_config_file) << "{\"result_count\": -4}";
    assert(config.load(temp_config_file.string()) && config.get_result_count() == ConfigManager::kMinResultCount);
    std::ofstream(temp_config_file) << "{\"result_count\": 99999999999999999999}";  // overflow
    assert(config.load(temp_config_file.string()) && config.get_result_count() == ConfigManager::kMaxResultCount);
    std::ofstream(temp_config_file) << "{\"result_count\": \"9\"}";  // wrong type -> default
    assert(config.load(temp_config_file.string()) && config.get_result_count() == 7);
    std::ofstream(temp_config_file) << "{\"result_count\": 7.5}";  // not an integer -> default
    assert(config.load(temp_config_file.string()) && config.get_result_count() == 7);

    // Test 18 (P8.6): disabled_providers default, toggle, round trip, wrong types, unknown ids kept
    std::ofstream(temp_config_file) << "{}";
    assert(config.load(temp_config_file.string()) && config.is_provider_enabled("shell"));
    config.set_provider_enabled("shell", false);  // saves
    config.set_provider_enabled("shell", false);  // idempotent
    config.set_provider_enabled("future-plugin", false);
    assert(config.load(temp_config_file.string()));
    assert(!config.is_provider_enabled("shell") && !config.is_provider_enabled("future-plugin"));
    assert(config.is_provider_enabled("files"));
    config.set_provider_enabled("shell", true);
    assert(config.load(temp_config_file.string()) && config.is_provider_enabled("shell"));
    assert(!config.is_provider_enabled("future-plugin"));
    std::ofstream(temp_config_file) << "{\"disabled_providers\": \"files\"}";  // wrong type -> none disabled
    assert(config.load(temp_config_file.string()) && config.is_provider_enabled("files"));
    std::ofstream(temp_config_file) << "{\"disabled_providers\": [3, \"\", \"files\"]}";  // junk items skipped
    assert(config.load(temp_config_file.string()) && !config.is_provider_enabled("files") && config.is_provider_enabled("shell"));
    std::ofstream(temp_config_file) << "{}";  // absent after a load -> back to all enabled
    assert(config.load(temp_config_file.string()) && config.is_provider_enabled("files"));

    // Test 19 (P8.6): set_file_exclusions saves, drops invalid names and duplicates
    config.set_file_exclusions({"vendor", "a/b", "vendor", "bad\"q", "tmp"});
    assert((config.get_file_exclusions() == std::vector<std::string>{"vendor", "tmp"}));
    assert(config.load(temp_config_file.string()));
    assert((config.get_file_exclusions() == std::vector<std::string>{"vendor", "tmp"}));
    config.set_file_exclusions({});
    assert(config.load(temp_config_file.string()) && config.get_file_exclusions().empty());

    // Cleanup
    std::filesystem::remove(temp_config_file);
    fs::remove(temp_config_file.string() + ".malformed.1");  // from Test 8's wrong-shape file

    std::cout << "All Config tests passed successfully!\n";
    return 0;
}

