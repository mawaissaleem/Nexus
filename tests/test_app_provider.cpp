#include "nexus/core/action_dispatcher.hpp"
#include "nexus/providers/app_provider.hpp"
#include <atomic>
#include <ctime>
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
    write_desktop(dir, "interm", "frobnicate", "Terminal=true\n");

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
    // Terminal flag travels on the result to the dispatcher
    assert(find(results, "interm") && find(results, "interm")->terminal);
    assert(find(results, "interm")->action_kind == core::ActionKind::Launch);
    assert(!find(results, "byname")->terminal && find(results, "byname")->action_kind == core::ActionKind::Launch);

    // Zero usage: typed signals unset
    assert(find(results, "byname")->usage_count == 0);
    assert(find(results, "byname")->metadata.count("desktop_file") == 1);

    // Recorded usage: key present with the right count (action() is never called)
    assert(db->record_usage("byname", "frobnicate"));
    assert(db->record_usage("byname", "frob"));
    results = provider.search(core::Query("frobnicate"), cancel);
    assert(find(results, "byname")->usage_count == 2);
    assert(find(results, "bykeyword")->usage_count == 0);
    assert(find(results, "bykeyword")->last_used == 0);
    {
        const long long last = find(results, "byname")->last_used;
        const long long now = static_cast<long long>(std::time(nullptr));
        assert(last >= now - 5 && last <= now + 5);
    }
    // Unrelated query: "frobnicate"/"frob" launches don't boost for a query that shares no prefix
    results = provider.search(core::Query("nicate"), cancel);
    assert(find(results, "byname") && find(results, "byname")->usage_count == 0);

    // Non-general mode and cancelled token yield nothing
    assert(provider.search(core::Query("> frobnicate", ">"), cancel).empty());
    cancel = true;
    assert(provider.search(core::Query("frobnicate"), cancel).empty());

    // History switch: executing a result records only while the switch is on, and flips take effect at once.
    // Exec points at a missing binary so no real application starts.
    std::ofstream(dir / "histapp.desktop") << "[Desktop Entry]\nType=Application\nName=histapp\nExec=/nonexistent/nexus_test_no_such_binary\n";
    assert(indexer->rebuild_index() >= 6);
    std::atomic<bool> hist_on{false};
    auto hist_ptr = std::make_shared<providers::ApplicationProvider>(indexer, db, [&] { return hist_on.load(); });
    auto& hist_provider = *hist_ptr;
    std::atomic<bool> go{false};
    const auto run_histapp = [&] {
        auto rs = hist_provider.search(core::Query("histapp"), go);
        const auto* r = find(rs, "histapp");
        assert(r && r->action_kind == core::ActionKind::Launch);
        core::dispatch_action(*r, {hist_ptr});
    };
    run_histapp();  // off: nothing written
    assert(db->get_usage_count("histapp") == 0);
    hist_on = true;
    run_histapp();  // on
    assert(db->get_usage_count("histapp") == 1);
    hist_on = false;
    run_histapp();  // switched off again at runtime
    assert(db->get_usage_count("histapp") == 1);
    // query_history is written next to usage_events: clearing reports both
    const auto cleared = db->clear_history();
    assert(cleared && cleared->queries == 1);

    old_home ? setenv("HOME", s_home.c_str(), 1) : unsetenv("HOME");
    old_xdh ? setenv("XDG_DATA_HOME", s_xdh.c_str(), 1) : unsetenv("XDG_DATA_HOME");
    old_xdd ? setenv("XDG_DATA_DIRS", s_xdd.c_str(), 1) : unsetenv("XDG_DATA_DIRS");
    fs::remove_all(root);
    return 0;
}
