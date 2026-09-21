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

    auto& cfg = ConfigManager::instance();

    // ConfigManager is a singleton backed by the REAL, persistent user config
    // file (~/.config/nexus/config.json), loaded automatically the moment
    // this singleton is first constructed. If the person running this test
    // has ever configured real search directories (e.g. via `nexus dir add`,
    // as the README itself demonstrates), those directories are already
    // loaded here — and FileProvider would search them too, alongside the
    // test fixture, making results non-deterministic and dependent on
    // whatever happens to exist on the developer's real machine.
    //
    // To keep this test fully isolated, save whatever real directories are
    // currently configured, remove them for the duration of this test, and
    // restore them exactly afterward — so the test never permanently alters
    // the person's actual Nexus configuration.
    const std::vector<std::string> original_dirs = cfg.get_search_directories();
    for (const auto& dir : original_dirs) {
        cfg.remove_search_directory(dir);
    }

    cfg.add_search_directory(test_base.string());

    FileProvider provider;
    std::atomic<bool> cancel{false};

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

    // Clean up: remove the test fixture and restore the real search
    // directories exactly as they were before this test ran.
    cfg.remove_search_directory(test_base.string());
    for (const auto& dir : original_dirs) {
        cfg.add_search_directory(dir);
    }
    std::filesystem::remove_all(test_base);

    std::cout << "All File/Directory search tests passed successfully!\n";
    return 0;
}
