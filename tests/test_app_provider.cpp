#include "nexus/providers/app_provider.hpp"
#include <atomic>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus;

static void write_desktop(const fs::path& dir, const std::string& id, const std::string& name,
                          const std::string& extra = "") {
    std::ofstream(dir / (id + ".desktop"))
        << "[Desktop Entry]\nType=Application\nName=" << name << "\nExec=" << id << "\n" << extra;
}

static const core::SearchResult* find(const std::vector<core::SearchResult>& rs, const std::string& id) {
    for (const auto& r : rs) if (r.id == id) return &r;
    return nullptr;
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_app_provider_" + std::to_string(getpid()));
    fs::remove_all(root);
    const fs::path dir = root / "sys" / "applications";
    fs::create_directories(dir);

    const char* old_home = std::getenv("HOME");
    const char* old_xdh = std::getenv("XDG_DATA_HOME");
    const char* old_xdd = std::getenv("XDG_DATA_DIRS");
    const std::string s_home = old_home ? old_home : "", s_xdh = old_xdh ? old_xdh : "", s_xdd = old_xdd ? old_xdd : "";
    setenv("HOME", (root / "fakehome").c_str(), 1);
    setenv("XDG_DATA_HOME", (root / "home").c_str(), 1);
    setenv("XDG_DATA_DIRS", (root / "sys").c_str(), 1);

    // Each app matches the query "frobnicate" in exactly one field, with the field equal to the
    // query so the raw fuzzy score is identical and only the field multiplier differs.
    write_desktop(dir, "byname", "frobnicate");
    write_desktop(dir, "bygeneric", "Alpha", "GenericName=frobnicate\n");
    write_desktop(dir, "bykeyword", "Beta", "Keywords=frobnicate;\n");
    write_desktop(dir, "bycomment", "Gamma", "Comment=frobnicate\n");
    write_desktop(dir, "nomatch", "Delta");

    auto db = std::make_shared<index::Database>((root / "nexus.db").string());
    assert(db->open());
    auto indexer = std::make_shared<index::AppIndexer>(db);
    assert(indexer->rebuild_index() >= 5);  // host snap/flatpak dirs may add more
    providers::ApplicationProvider provider(indexer, db);
    std::atomic<bool> cancel{false};

    auto results = provider.search(core::Query("frobnicate"), cancel);
    assert(find(results, "byname") && find(results, "bygeneric") && find(results, "bykeyword") && find(results, "bycomment"));
    assert(!find(results, "nomatch"));
    const double name = find(results, "byname")->score;
    const double generic = find(results, "bygeneric")->score;
    const double keyword = find(results, "bykeyword")->score;
    const double comment = find(results, "bycomment")->score;
    // name > generic name (x0.8) > keyword (x0.75) > comment (x0.5)
    assert(name > generic && generic > keyword && keyword > comment && comment > 0.0);
    assert(find(results, "bygeneric")->subtitle == "frobnicate");

    // Zero usage: metadata key absent
    assert(find(results, "byname")->metadata.count("usage_count") == 0);
    assert(find(results, "byname")->metadata.count("desktop_file") == 1);

    // Recorded usage: key present with the right count (action() is never called)
    assert(db->record_usage("byname", "frobnicate"));
    assert(db->record_usage("byname", "frob"));
    results = provider.search(core::Query("frobnicate"), cancel);
    assert(find(results, "byname")->metadata.at("usage_count") == "2");
    assert(find(results, "bykeyword")->metadata.count("usage_count") == 0);

    // Non-general mode and cancelled token yield nothing
    assert(provider.search(core::Query("> frobnicate"), cancel).empty());
    cancel = true;
    assert(provider.search(core::Query("frobnicate"), cancel).empty());

    old_home ? setenv("HOME", s_home.c_str(), 1) : unsetenv("HOME");
    old_xdh ? setenv("XDG_DATA_HOME", s_xdh.c_str(), 1) : unsetenv("XDG_DATA_HOME");
    old_xdd ? setenv("XDG_DATA_DIRS", s_xdd.c_str(), 1) : unsetenv("XDG_DATA_DIRS");
    fs::remove_all(root);
    return 0;
}
