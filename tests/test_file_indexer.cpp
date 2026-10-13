#include "nexus/core/config_manager.hpp"
#include "nexus/core/query.hpp"
#include "nexus/index/database.hpp"
#include "nexus/index/file_indexer.hpp"
#include "nexus/providers/file_provider.hpp"
#include "nexus/utils/fuzzy.hpp"
#include <atomic>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus;

namespace {

const std::atomic<bool> kNoCancel{false};

void touch(const fs::path& p) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << "x";
}

std::vector<core::SearchResult> search(providers::FileProvider& fp, const std::string& q) {
    return fp.search(core::Query(q, "file "), kNoCancel);
}

bool has_title(const std::vector<core::SearchResult>& r, const std::string& title) {
    for (const auto& x : r) if (x.title == title) return true;
    return false;
}

} // namespace

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_test_file_indexer_" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path tree = root / "tree";
    touch(tree / "my_project" / "main.cpp");
    touch(tree / "docs" / "thesis.pdf");
    touch(tree / "node_modules" / "left_pad.js");      // default exclusion
    touch(tree / "build" / "artifact.o");              // default exclusion
    touch(tree / ".hidden" / "secret.txt");            // hidden dir
    touch(tree / "docs" / ".dotfile");                 // hidden file
    touch(tree / "a" / "b" / "c" / "d" / "deep.txt");  // depth 4, beyond default max depth 3
    touch(tree / "a" / "b" / "c" / "shallow.txt");     // depth 3
    touch(tree / "50%_off" / "a_b.txt");               // LIKE wildcards in names
    touch(tree / "intl" / "résumé.txt");               // multi-byte UTF-8 names
    touch(tree / "intl" / "Über.pdf");

    auto db = std::make_shared<index::Database>((root / "t.db").string());
    assert(db->open());
    index::FileIndexer indexer(db);
    providers::FileProvider provider(db);

    index::FileIndexOptions opts{{tree.string()}, core::ConfigManager::default_file_exclusions(), 3};

    // Before any build: queries return nothing and nothing blows up.
    assert(search(provider, "file thesis").empty());

    // Build: files and folders land in the DB; default exclusions, hidden entries and depth limit are honoured.
    size_t n = indexer.rebuild(opts, kNoCancel);
    assert(n > 0);
    assert(has_title(search(provider, "file thesis"), "thesis.pdf"));
    assert(search(provider, "file my_project")[0].title == "my_project");
    assert(search(provider, "file my_project")[0].icon == "folder");
    assert(search(provider, "file left_pad").empty());
    assert(search(provider, "file artifact").empty());
    assert(search(provider, "file secret").empty());
    assert(search(provider, "file dotfile").empty());
    assert(has_title(search(provider, "file shallow"), "shallow.txt"));
    assert(search(provider, "file deep").empty());
    // The search root itself is a result and outranks a same-named child.
    assert(search(provider, "file tree")[0].subtitle == tree.string());
    // Short terms and non-file modes never hit the index.
    assert(search(provider, "file t").empty());
    assert(search(provider, "thesis").empty());
    // '%' and '_' are literal characters, not LIKE wildcards.
    assert(has_title(search(provider, "file a_b"), "a_b.txt"));
    assert(search(provider, "file a%b").empty());
    // Non-ASCII terms: exact, substring and subsequence hits (the LIKE prefilter must not split UTF-8 characters).
    assert(has_title(search(provider, "file résumé"), "résumé.txt"));
    assert(has_title(search(provider, "file sumé"), "résumé.txt"));
    assert(has_title(search(provider, "file rsmé"), "résumé.txt"));
    assert(has_title(search(provider, "file Über"), "Über.pdf"));
    assert(has_title(search(provider, "file Üpdf"), "Über.pdf"));
    // Case folding is ASCII-only, exactly like FuzzyMatcher: "über" does not find "Über".
    assert(!utils::FuzzyMatcher::match("über", "Über.pdf").matched);
    assert(search(provider, "file über").empty());
    assert(has_title(search(provider, "file RÉSUMÉ"), "résumé.txt") == false);

    // Depth 0 = direct children only.
    index::FileIndexOptions shallow = opts;
    shallow.max_depth = 0;
    indexer.rebuild(shallow, kNoCancel);
    assert(has_title(search(provider, "file thesis"), "docs") == false);
    assert(search(provider, "file thesis").empty());
    assert(has_title(search(provider, "file docs"), "docs"));

    // Custom exclusions replace the default list.
    index::FileIndexOptions custom = opts;
    custom.exclusions = {"docs"};
    indexer.rebuild(custom, kNoCancel);
    assert(search(provider, "file thesis").empty());
    assert(has_title(search(provider, "file left_pad"), "left_pad.js"));  // no longer excluded

    // Queries read the DB, not the disk: delete from disk, still found until the next rebuild.
    indexer.rebuild(opts, kNoCancel);
    fs::remove_all(tree / "docs");
    assert(has_title(search(provider, "file thesis"), "thesis.pdf"));
    // Rebuild replaces old rows, and picks up new files.
    touch(tree / "fresh" / "newfile.txt");
    indexer.rebuild(opts, kNoCancel);
    assert(search(provider, "file thesis").empty());
    assert(has_title(search(provider, "file newfile"), "newfile.txt"));

    // Cancelled build: returns 0 and leaves the previous index intact.
    std::atomic<bool> cancelled{true};
    assert(indexer.rebuild(opts, cancelled) == 0);
    assert(has_title(search(provider, "file newfile"), "newfile.txt"));

    // Cancelled query returns no results.
    assert(provider.search(core::Query("file newfile", "file "), cancelled).empty());

    // A missing search directory is skipped without error.
    index::FileIndexOptions missing{{(root / "nope").string(), tree.string()}, opts.exclusions, 3};
    assert(indexer.rebuild(missing, kNoCancel) > 0);

    fs::remove_all(root);
    std::cout << "All FileIndexer tests passed successfully!\n";
    return 0;
}
