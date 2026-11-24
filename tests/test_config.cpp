#include "nexus/core/config_manager.hpp"
#include "check.hpp"
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
    // Defaults come from the constructor, so chdir first; the config file is a temp path (never ~/.config/nexus).
    const fs::path work = fs::temp_directory_path() / ("nexus_cfg_cwd_" + pid);
    const fs::path old_cwd = fs::current_path();
    fs::create_directories(work);
    fs::current_path(work);

    ConfigManager config(temp_config_file.string());
    const std::string cwd = fs::current_path().string();
    for (const auto& d : config.get_search_directories()) assert(d != cwd);

    fs::current_path(old_cwd);
    fs::remove_all(work);

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
    std::ofstream(temp_config_file)
        << "{\n  \"enable_ctrl_np_navigation\": false,\n  \"global_shortcut\": \"Ctrl+K\"\n";  // unterminated
    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == true);
    assert(config.get_shortcut() == "Alt+Space");
    assert(config.get_file_exclusions() == ConfigManager::default_file_exclusions());
    std::ofstream(temp_config_file) << "[1, 2]";  // valid JSON, wrong shape
    assert(config.load(temp_config_file.string()) == false);

    // Test 9: one-line arrays parse
    std::ofstream(temp_config_file) << "{\"file_exclusions\": [\"a\", \"b\"], \"search_directories\": [\"/tmp\"]}";
    assert(config.load(temp_config_file.string()));
    assert((config.get_file_exclusions() == std::vector<std::string>{"a", "b"}));
    assert((config.get_search_directories() == std::vector<std::string>{"/tmp"}));

    // Test 10: keys absent from the file reset to defaults instead of keeping the last load
    std::ofstream(temp_config_file) << "{\"enable_ctrl_np_navigation\": false}";
    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == false);
    std::ofstream(temp_config_file) << "{}";
    config.load(temp_config_file.string());
    assert(config.is_ctrl_np_navigation_enabled() == true);
    assert(config.get_file_exclusions() == ConfigManager::default_file_exclusions());

    // Test 11: directories with quote/backslash round-trip through save/load
    const fs::path odd = fs::temp_directory_path() / ("nexus_cfg_odd\\\"dir_" + pid);
    fs::create_directories(odd);
    assert(config.add_search_directory(odd.string()));
    config.load(temp_config_file.string());
    auto dirs = config.get_search_directories();
    assert(std::find(dirs.begin(), dirs.end(), fs::canonical(odd).string()) != dirs.end());
    fs::remove_all(odd);

    // Test 12: a file in the old writer's format (unescaped, one item per line) still loads
    std::ofstream(temp_config_file)
        << "{\n  \"enable_ctrl_np_navigation\": false,\n  \"global_shortcut\": \"Ctrl+Shift+K\",\n"
           "  \"search_directories\": [\n    \"/tmp\",\n    \"/usr\"\n  ],\n"
           "  \"file_exclusions\": [\n    \"node_modules\"\n  ]\n}\n";
    assert(config.load(temp_config_file.string()));
    assert(!config.is_ctrl_np_navigation_enabled());
    assert(config.get_shortcut() == "Ctrl+Shift+K");
    assert((config.get_search_directories() == std::vector<std::string>{"/tmp", "/usr"}));
    assert((config.get_file_exclusions() == std::vector<std::string>{"node_modules"}));

    // Test 13: a 0.3.0 file with an unescaped backslash path still loads (lenient unknown escape)
    std::ofstream(temp_config_file)
        << "{\n  \"enable_ctrl_np_navigation\": false,\n  \"global_shortcut\": \"Ctrl+Shift+K\",\n"
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

    // Test 17: result_count default, round trip, clamping, invalid values
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

    // Test 18: disabled_providers default, toggle, round trip, wrong types, unknown ids kept
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
    assert(config.load(temp_config_file.string()) && !config.is_provider_enabled("files") &&
           config.is_provider_enabled("shell"));
    std::ofstream(temp_config_file) << "{}";  // absent after a load -> back to all enabled
    assert(config.load(temp_config_file.string()) && config.is_provider_enabled("files"));

    // Test 19: set_file_exclusions saves, drops invalid names and duplicates
    config.set_file_exclusions({"vendor", "a/b", "vendor", "bad\"q", "tmp"});
    assert((config.get_file_exclusions() == std::vector<std::string>{"vendor", "tmp"}));
    assert(config.load(temp_config_file.string()));
    assert((config.get_file_exclusions() == std::vector<std::string>{"vendor", "tmp"}));
    config.set_file_exclusions({});
    assert(config.load(temp_config_file.string()) && config.get_file_exclusions().empty());

    // Test 17: clipboard_retention defaults to 50, is clamped, and survives a save by another setter
    std::ofstream(temp_config_file) << "{}";
    assert(config.load(temp_config_file.string()) && config.get_clipboard_retention() == 50);
    std::ofstream(temp_config_file) << "{\"clipboard_retention\": 5}";
    assert(config.load(temp_config_file.string()) && config.get_clipboard_retention() == 5);
    config.set_history_enabled(true);  // rewrites the file
    assert(config.load(temp_config_file.string()) && config.get_clipboard_retention() == 5);
    std::ofstream(temp_config_file) << "{\"clipboard_retention\": 0}";
    assert(config.load(temp_config_file.string()) && config.get_clipboard_retention() == 1);
    std::ofstream(temp_config_file) << "{\"clipboard_retention\": 99999999999999}";
    assert(config.load(temp_config_file.string()) && config.get_clipboard_retention() == 1000);
    std::ofstream(temp_config_file) << "{\"clipboard_retention\": \"ten\"}";
    assert(config.load(temp_config_file.string()) && config.get_clipboard_retention() == 50);

    // No usable per-user config dir: planted files in cwd are ignored, nothing is written to cwd or /tmp.
    {
        const fs::path planted_dir = fs::temp_directory_path() / ("nexus_config_planted_" + pid);
        const fs::path abs_home = fs::temp_directory_path() / ("nexus_config_home_" + pid);
        fs::remove_all(planted_dir);
        fs::remove_all(abs_home);
        fs::create_directories(planted_dir);
        const fs::path saved_cwd = fs::current_path();
        fs::current_path(planted_dir);
        // The old cwd fallback, and where XDG_CONFIG_HOME=rel or HOME=. would resolve if honoured.
        const fs::path planted[] = {".nexus_config.json", fs::path("rel") / "nexus" / "config.json",
                                    fs::path(".config") / "nexus" / "config.json"};
        const std::string payload = "{\"result_count\": 13}";
        for (const auto& p : planted) {
            if (p.has_parent_path()) fs::create_directories(p.parent_path());
            std::ofstream(p) << payload;
        }
        const fs::path home_file = planted_dir / "home_is_a_file";
        std::ofstream(home_file) << "x";
        const auto entries_before = std::distance(fs::directory_iterator(planted_dir), fs::directory_iterator{});
        const bool tmp_existed = fs::exists("/tmp/nexus");

        std::optional<std::string> saved_home;
        std::optional<std::string> saved_xdg;
        if (const char* v = std::getenv("HOME")) saved_home = v;
        if (const char* v = std::getenv("XDG_CONFIG_HOME")) saved_xdg = v;
        unsetenv("HOME");
        unsetenv("XDG_CONFIG_HOME");
        CHECK_EQ(ConfigManager::resolve_config_path(), std::string());

        // Relative values count as unset.
        setenv("XDG_CONFIG_HOME", "rel", 1);
        CHECK_EQ(ConfigManager::resolve_config_path(), std::string());
        setenv("HOME", ".", 1);
        CHECK_EQ(ConfigManager::resolve_config_path(), std::string());
        // Default search dirs come from HOME too: none may be relative (cwd would get indexed).
        const ConfigManager rel_home((planted_dir / "absent.json").string());
        for (const auto& d : rel_home.get_search_directories()) CHECK_EQ(fs::path(d).is_absolute(), true);
        unsetenv("XDG_CONFIG_HOME");
        CHECK_EQ(ConfigManager::resolve_config_path(), std::string());

        // A relative XDG_CONFIG_HOME falls back to an absolute HOME.
        setenv("XDG_CONFIG_HOME", "rel", 1);
        setenv("HOME", abs_home.c_str(), 1);
        CHECK_EQ(ConfigManager::resolve_config_path(), (abs_home / ".config" / "nexus" / "config.json").string());
        unsetenv("XDG_CONFIG_HOME");

        // HOME that is a regular file: the config dir cannot be created.
        setenv("HOME", home_file.c_str(), 1);
        CHECK_EQ(ConfigManager::resolve_config_path(), std::string());
        unsetenv("HOME");

        ConfigManager unusable(ConfigManager::resolve_config_path());
        CHECK_EQ(unusable.get_result_count() == 13, false);
        CHECK_EQ(unusable.save(), false);

        // Unwritable path: save fails and does not fall back to cwd.
        ConfigManager unwritable((planted_dir / "missing_dir" / "x.json").string());
        CHECK_EQ(unwritable.save(), false);

        for (const auto& p : planted) CHECK_EQ(fs::file_size(p), payload.size());
        CHECK_EQ(fs::exists("/tmp/nexus"), tmp_existed);
        CHECK_EQ(std::distance(fs::directory_iterator(planted_dir), fs::directory_iterator{}), entries_before);

        if (saved_home) setenv("HOME", saved_home->c_str(), 1);
        if (saved_xdg) setenv("XDG_CONFIG_HOME", saved_xdg->c_str(), 1);
        fs::current_path(saved_cwd);
        fs::remove_all(planted_dir);
        fs::remove_all(abs_home);
    }

    // Cleanup
    std::filesystem::remove(temp_config_file);
    fs::remove(temp_config_file.string() + ".malformed.1");  // from Test 8's wrong-shape file

    std::cout << "All Config tests passed successfully!\n";
    return 0;
}
