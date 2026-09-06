#include "nexus/core/config_manager.hpp"
#include "nexus/providers/file_provider.hpp"
#include "nexus/core/query.hpp"
#include <iostream>
#include <fstream>
#include <cassert>
#include <atomic>
#include <filesystem>

int main() {
    using namespace nexus::core;
    using namespace nexus::providers;

    std::filesystem::path test_base = std::filesystem::temp_directory_path() / "nexus_test_file_search";
    std::filesystem::create_directories(test_base / "my_project");
    std::filesystem::create_directories(test_base / "docs_folder");

    {
        std::ofstream(test_base / "my_project" / "main.cpp") << "// code";
        std::ofstream(test_base / "docs_folder" / "thesis.pdf") << "%PDF";
    }

    auto& cfg = ConfigManager::instance();
    cfg.add_search_directory(test_base.string());

    FileProvider provider;
    std::atomic<bool> cancel{false};

    // Test 1: Search directory name
    {
        Query q("my_project");
        auto results = provider.search(q, cancel);
        assert(!results.empty());
        assert(results[0].title == "my_project");
        assert(results[0].icon == "folder");
    }

    // Test 2: Search file inside directory
    {
        Query q("thesis");
        auto results = provider.search(q, cancel);
        assert(!results.empty());
        assert(results[0].title == "thesis.pdf");
        assert(results[0].icon == "application-pdf");
    }

    // Test 3: Explicit file mode
    {
        Query q("file main");
        auto results = provider.search(q, cancel);
        assert(!results.empty());
        assert(results[0].title == "main.cpp");
    }

    // Clean up
    cfg.remove_search_directory(test_base.string());
    std::filesystem::remove_all(test_base);

    std::cout << "All File/Directory search tests passed successfully!\n";
    return 0;
}
