#include "nexus/core/alias_manager.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/core/query.hpp"
#include <iostream>
#include <cassert>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <cstdlib>
#include <unistd.h>

int main() {
    using namespace nexus::core;
    using namespace nexus::providers;

    namespace fs = std::filesystem;
    const std::string pid = std::to_string(getpid());
    fs::path temp_alias_file = fs::temp_directory_path() / ("nexus_test_aliases_" + pid + ".json");
    fs::remove(temp_alias_file);

    // The singleton constructor loads the default aliases file: point it at a temp dir, never ~/.config/nexus.
    const fs::path xdg = fs::temp_directory_path() / ("nexus_alias_xdg_" + pid);
    const char* old_xdg_env = std::getenv("XDG_CONFIG_HOME");
    const std::optional<std::string> old_xdg = old_xdg_env ? std::optional<std::string>(old_xdg_env) : std::nullopt;
    setenv("XDG_CONFIG_HOME", xdg.c_str(), 1);
    auto& mgr = AliasManager::instance();
    if (old_xdg) setenv("XDG_CONFIG_HOME", old_xdg->c_str(), 1); else unsetenv("XDG_CONFIG_HOME");
    mgr.load(temp_alias_file.string());

    // Test 1: Type detection
    assert(AliasManager::detect_type("https://github.com") == AliasType::Url);
    assert(AliasManager::detect_type("http://example.com") == AliasType::Url);
    assert(AliasManager::detect_type("/tmp") == AliasType::Directory);
    assert(AliasManager::detect_type("docker ps") == AliasType::Command);

    // Test 2: Add and list aliases
    assert(mgr.add_alias("gh", "https://github.com"));
    assert(mgr.add_alias("tmpdir", "/tmp"));
    assert(mgr.add_alias("ls_cmd", "ls -la"));

    auto list = mgr.get_aliases();
    assert(list.size() >= 3);

    // Test 3: Search with AliasProvider
    AliasProvider provider;
    std::atomic<bool> cancel{false};

    Query q("gh");
    auto results = provider.search(q, cancel);
    assert(!results.empty());
    assert(results[0].title == "gh");
    assert(results[0].score >= 1500.0);
    assert(results[0].icon == "web-browser");

    // Test 4: Remove alias
    assert(mgr.remove_alias("ls_cmd"));
    Query q_removed("ls_cmd");
    auto res_removed = provider.search(q_removed, cancel);
    assert(res_removed.empty());

    // Test 5: quotes/backslashes round-trip; malformed file is rejected without losing aliases
    assert(mgr.add_alias("q", "echo \"hi\" \\n"));
    mgr.load(temp_alias_file.string());
    bool found = false;
    for (const auto& a : mgr.get_aliases()) if (a.name == "q") { found = true; assert(a.target == "echo \"hi\" \\n"); }
    assert(found);
    { std::ofstream(temp_alias_file) << "[{\"name\": \"x\", "; }
    assert(!mgr.load(temp_alias_file.string()));
    assert(!mgr.get_aliases().empty());

    // Test 6: the malformed file was kept aside, so the save() below cannot destroy the only copy
    assert(mgr.add_alias("after", "/tmp"));
    {
        std::ifstream in(temp_alias_file.string() + ".malformed");
        assert(std::string(std::istreambuf_iterator<char>(in), {}) == "[{\"name\": \"x\", ");
    }

    // Test 7: a 0.3.0 file with an unescaped backslash still loads
    { std::ofstream(temp_alias_file) << "[\n  {\n    \"name\": \"win\",\n    \"target\": \"/srv/my\\dir\"\n  }\n]\n"; }
    assert(mgr.load(temp_alias_file.string()));
    assert(mgr.get_aliases().size() == 1 && mgr.get_aliases()[0].target == "/srv/my\\dir");

    // Clean up
    fs::remove(temp_alias_file);
    fs::remove(temp_alias_file.string() + ".malformed");
    fs::remove_all(xdg);

    std::cout << "All Alias tests passed successfully!\n";
    return 0;
}
