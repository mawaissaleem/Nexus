#include "nexus/core/alias_manager.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/core/query.hpp"
#include <iostream>
#include <cassert>
#include <atomic>
#include <filesystem>

int main() {
    using namespace nexus::core;
    using namespace nexus::providers;

    std::filesystem::path temp_alias_file = std::filesystem::temp_directory_path() / "nexus_test_aliases.json";
    std::filesystem::remove(temp_alias_file);

    auto& mgr = AliasManager::instance();
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

    // Clean up
    std::filesystem::remove(temp_alias_file);

    std::cout << "All Alias tests passed successfully!\n";
    return 0;
}
