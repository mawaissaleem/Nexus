#include "nexus/core/config_manager.hpp"
#include "nexus/index/database.hpp"
#include "nexus/index/file_indexer.hpp"
#include "nexus/providers/file_provider.hpp"
#include "nexus/core/query.hpp"
#include <iostream>
#include <fstream>
#include <cassert>
#include <atomic>
#include <filesystem>
#include <unistd.h>

int main() {
    using namespace nexus::core;
    using namespace nexus::providers;

    std::filesystem::path test_base = std::filesystem::temp_directory_path() / ("nexus_test_file_search_" + std::to_string(getpid()));

    // Clean any leftover state from a previous crashed/aborted run before
    // setting up fresh fixtures. Without this, an earlier assertion failure
    // (which skips the cleanup at the end of this file) leaves stale files
    // in this fixed, reused path, causing this test to become flaky on any
    // machine where it has previously failed — even though the code under
    // test is unaffected.
    std::filesystem::remove_all(test_base);
    std::filesystem::create_directories(test_base / "my_project");
    std::filesystem::create_directories(test_base / "docs_folder");

    {
        std::ofstream(test_base / "my_project" / "main.cpp") << "// code";
        std::ofstream(test_base / "docs_folder" / "thesis.pdf") << "%PDF";
    }

    // FileProvider now reads only the DB index. Build it straight from the fixture dir with
    // explicit options, so the real user config (~/.config/nexus) is never read or modified.
    auto db = std::make_shared<nexus::index::Database>((test_base / "test.db").string());
    assert(db->open());
    std::atomic<bool> cancel{false};
    nexus::index::FileIndexer indexer(db);
    assert(indexer.rebuild({{test_base.string()},
                            ConfigManager::default_file_exclusions(), 3}, cancel) > 0);

    FileProvider provider(db);

    // Test 1: Search directory name (explicit "file " prefix required —
    // FileProvider no longer responds to General-mode queries, see file_provider.cpp)
    {
        Query q("file my_project");
        auto results = provider.search(q, cancel);
        assert(!results.empty());
        assert(results[0].title == "my_project");
        assert(results[0].icon == "folder");
    }

    // Test 2: Search file inside directory (explicit "file " prefix required)
    {
        Query q("file thesis");
        auto results = provider.search(q, cancel);
        assert(!results.empty());
        assert(results[0].title == "thesis.pdf");
        assert(results[0].icon == "application-pdf");
    }

    // Test 4: General-mode queries must NOT trigger a file search
    // (this is the actual behavior being fixed for 0.1.0 — file search must
    // only run behind the explicit "file " prefix, never on plain typing)
    {
        Query q("my_project");
        auto results = provider.search(q, cancel);
        assert(results.empty());
    }

    // Test 3: Explicit file mode
    {
        Query q("file main");
        auto results = provider.search(q, cancel);
        assert(!results.empty());
        assert(results[0].title == "main.cpp");
    }

    // Clean up the fixture.
    std::filesystem::remove_all(test_base);

    std::cout << "All File/Directory search tests passed successfully!\n";
    return 0;
}
