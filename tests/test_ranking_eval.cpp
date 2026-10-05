// Ranking-quality regression gate: fixed query -> expected top result.
// Each case builds a tiny hermetic world (apps as .desktop files in a temp dir, files and usage in an
// in-memory DB, aliases on top of the built-in defaults) and runs the production SearchEngine with all
// five real providers. So every competitor is one the real providers return in that mode, with the
// score they give it; nothing reads ~/.config, ~/.local/share or launches anything.
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/search_engine.hpp"
#include "nexus/index/app_indexer.hpp"
#include "nexus/index/database.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/providers/app_provider.hpp"
#include "nexus/providers/calc_provider.hpp"
#include "nexus/providers/file_provider.hpp"
#include "nexus/providers/shell_provider.hpp"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <vector>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus;

struct App {
    std::string id, name;
    int usage{0}; // launches, recorded with the case's query so per-query history (P5.1) sees them too
};

struct Case {
    std::string name, query;
    std::vector<App> apps{};
    std::vector<index::FileEntry> files{};                       // build with file(): name comes from path
    std::vector<std::pair<std::string, std::string>> aliases{};  // added to the defaults (gh, google, nexus, docs, downloads)
    std::string expected_top;
    bool known_failure{false}; // ranking gets this wrong today; logged in nexus-decisions.md
};

static index::FileEntry file(std::string path, bool is_dir = false, bool is_root = false) {
    return {path, fs::path(path).filename().string(), is_dir, is_root};
}

static std::vector<core::SearchResult> run(const Case& c, const fs::path& dir) {
    fs::create_directories(dir);
    for (const auto& a : c.apps)
        std::ofstream(dir / (a.id + ".desktop")) << "[Desktop Entry]\nType=Application\nName=" << a.name
                                                 << "\nExec=" << a.id << "\n";
    auto indexer = std::make_shared<index::AppIndexer>(nullptr, std::vector<fs::path>{dir});
    indexer->rebuild_index();

    auto db = std::make_shared<index::Database>(":memory:");
    assert(db->open());
    for (const auto& a : c.apps)
        for (int i = 0; i < a.usage; ++i) assert(db->record_usage(a.id, c.query));
    std::vector<index::FileEntry> files = c.files;
    assert(db->replace_files(files));

    auto& aliases = core::AliasManager::instance();
    for (const auto& [n, t] : c.aliases) assert(aliases.add_alias(n, t));

    core::SearchEngine engine;
    engine.register_provider(std::make_shared<providers::AliasProvider>());
    engine.register_provider(std::make_shared<providers::ApplicationProvider>(indexer, db));
    engine.register_provider(std::make_shared<providers::FileProvider>(db));
    engine.register_provider(std::make_shared<providers::CalculatorProvider>());
    engine.register_provider(std::make_shared<providers::ShellProvider>());
    auto results = engine.search(core::Query(c.query));

    for (const auto& [n, t] : c.aliases) aliases.remove_alias(n);
    return results;
}

int main() {
    const fs::path root = fs::temp_directory_path() / ("nexus_ranking_eval_" + std::to_string(getpid()));
    fs::remove_all(root);

    // The alias singleton loads ~/.config/nexus/aliases.json and builds defaults from $HOME on first use:
    // point both at the temp dir for construction, then restore them.
    auto save_env = [](const char* k) { const char* v = std::getenv(k); return v ? std::optional<std::string>(v) : std::nullopt; };
    auto restore_env = [](const char* k, const std::optional<std::string>& v) { v ? setenv(k, v->c_str(), 1) : unsetenv(k); };
    const auto old_home = save_env("HOME"), old_xdg = save_env("XDG_CONFIG_HOME");
    setenv("HOME", (root / "home").c_str(), 1);
    setenv("XDG_CONFIG_HOME", (root / "config").c_str(), 1);
    core::AliasManager::instance().load((root / "aliases.json").string()); // missing file: defaults stay, saves go here
    restore_env("HOME", old_home);
    restore_env("XDG_CONFIG_HOME", old_xdg);

    const std::vector<Case> cases = {
        {.name = "exact beats prefix", .query = "firefox",
         .apps = {{"firefox-dev", "Firefox Developer Edition"}, {"firefox", "Firefox"}},
         .expected_top = "firefox"},
        {.name = "prefix beats substring", .query = "term",
         .apps = {{"xfce4-terminal", "Xfce Terminal"}, {"terminal", "Terminal"}},
         .expected_top = "terminal"},
        {.name = "acronym vsc finds Visual Studio Code (SRS FR-06)", .query = "vsc",
         .apps = {{"code", "Visual Studio Code"}, {"vinagre", "Vinagre Remote Desktop Client"}},
         .expected_top = "code"},
        // SRS 11: the user keeps picking VS Code for "vsc"; a never-used prefix hit still wins by
        // the +250 prefix boost against ~15 points of usage. P5.1 (query-conditioned usage) should fix it.
        {.name = "repeatedly chosen acronym beats never-used prefix hit (SRS 11)", .query = "vsc",
         .apps = {{"code", "Visual Studio Code", 20}, {"vscan", "Vscan Tool"}},
         .expected_top = "code", .known_failure = true},
        {.name = "more-used of two same-named apps wins", .query = "terminal",
         .apps = {{"gnome-terminal", "Terminal"}, {"xfce4-terminal", "Terminal", 40}},
         .expected_top = "xfce4-terminal"},
        {.name = "exact match not buried by heavy usage (SRS 11)", .query = "code",
         .apps = {{"exact", "Code"}, {"busy", "Code Review Helper", 500}},
         .expected_top = "exact"},
        {.name = "lightly used exact beats heavily used fuzzy hit (SRS 11)", .query = "gimp",
         .apps = {{"gimp", "GIMP", 1}, {"glimpse", "Glimpse", 1000}},
         .expected_top = "gimp"},
        // SRS 13: the calculator answers directly. Its title is the result ("4"), so it never gets a lexical
        // boost and its 750 raw score is worth only 120 points: any app whose title contains the expression wins.
        {.name = "inline arithmetic result beats app containing the expression (SRS 13)", .query = "2+2",
         .apps = {{"quiz", "Math 2+2 Quiz"}},
         .expected_top = "calc:4", .known_failure = true},
        {.name = "exact default alias beats app", .query = "gh",
         .apps = {{"github-desktop", "GitHub Desktop"}},
         .expected_top = "alias:gh"},
        {.name = "exact file name beats longer prefix file (SRS 15)", .query = "file thesis.pdf",
         .files = {file("/home/u/Documents/old/thesis.pdf.bak"), file("/home/u/Documents/thesis.pdf")},
         .expected_top = "file:/home/u/Documents/thesis.pdf"},
        // SRS 14/15: an explicit mode prefix says what the user wants, but AliasProvider answers in every
        // mode and an exact alias name outscores the mode's own result (alias 1056 vs file 1008, shell 210).
        {.name = "file mode: indexed file beats same-named alias (SRS 15)", .query = "file notes",
         .files = {file("/home/u/notes", true)},
         .aliases = {{"notes", "https://notes.example.com"}},
         .expected_top = "dir:/home/u/notes", .known_failure = true},
        {.name = "shell mode: command beats same-named default alias (SRS 14)", .query = ">gh",
         .expected_top = "shell:gh", .known_failure = true},
        // Typing "> dd if=..." passes through ">dd", which fuzzy-matches the default "downloads" alias.
        {.name = "shell mode: command beats fuzzy alias hit (SRS 14)", .query = ">dd",
         .expected_top = "shell:dd"},
    };

    int failures = 0, known = 0;
    for (size_t i = 0; i < cases.size(); ++i) {
        const auto& c = cases[i];
        const auto results = run(c, root / ("case" + std::to_string(i)));
        const bool ok = !results.empty() && results[0].id == c.expected_top;
        if (ok && !c.known_failure) continue;
        if (ok || !c.known_failure) ++failures; else ++known;
        std::cout << (ok ? "FIXED, remove known_failure: " : c.known_failure ? "KNOWN FAILURE: " : "FAIL: ") << c.name << "\n  query=\"" << c.query
                  << "\" expected=" << c.expected_top << "\n";
        for (size_t r = 0; r < std::min<size_t>(results.size(), 5); ++r)
            std::cout << "  #" << r + 1 << " " << results[r].id << " \"" << results[r].title
                      << "\" score=" << results[r].score << "\n";
    }

    fs::remove_all(root);
    std::cout << cases.size() << " cases, " << failures << " failed, " << known << " known failures\n";
    return failures == 0 ? 0 : 1;
}
