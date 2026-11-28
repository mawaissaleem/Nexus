// Things going wrong on a user's machine: each case must degrade as documented and never crash.
#include "check.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/core/gnome_keybinding.hpp"
#include "nexus/core/search_engine.hpp"
#include "nexus/index/database.hpp"
#include "nexus/index/file_indexer.hpp"
#include "nexus/providers/calc_provider.hpp"
#include "nexus/providers/plugin_provider.hpp"
#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <unistd.h>

namespace fs = std::filesystem;
using namespace nexus;

static fs::path root;
static const std::atomic<bool> no_cancel{false};

static void write_file(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path) << text;
}

// Not a database: open reports failure instead of throwing, and later calls are harmless no-ops.
static void corrupt_database() {
    const fs::path db_path = root / "corrupt.db";
    write_file(db_path, std::string(4096, 'x'));
    index::Database db(db_path.string());
    const utils::Status opened = db.open();
    assert(!opened);
    assert(!db.is_open());
    assert(opened.message().find("corrupt.db") != std::string::npos);
    assert(opened.message().find("not a database") != std::string::npos);
    assert(db.load_applications().empty());
    assert(db.find_files("x").empty());
    assert(!db.record_usage("app", "q"));
}

// A directory that cannot be read, or does not exist, is skipped; the other directories still index.
static void unreadable_search_dir() {
    const fs::path good = root / "good";
    const fs::path locked = root / "locked";
    write_file(good / "notes.txt", "");
    fs::create_directories(locked / "inner");
    fs::permissions(locked, fs::perms::none);

    auto db = std::make_shared<index::Database>((root / "files.db").string());
    assert(db->open());
    index::FileIndexer indexer(db);
    const index::FileIndexOptions options{{locked.string(), (root / "missing").string(), good.string()}, {}};
    indexer.rebuild(options, no_cancel);
    assert(!db->find_files("notes").empty());

    fs::permissions(locked, fs::perms::owner_all);
}

// Bad JSON means defaults, and the broken file is kept as <file>.malformed instead of being lost.
static void malformed_config() {
    const fs::path path = root / "config" / "config.json";
    // 12 is valid and not the default, so a parser that kept fields read before the error would show it.
    const std::string broken = "{ \"result_count\": 12, oops";
    write_file(path, broken);
    core::ConfigManager config(path.string());
    CHECK_EQ(config.get_result_count(), core::ConfigManager::kDefaultResultCount);
    assert(!config.load());

    std::ifstream backup(path.string() + ".malformed");
    assert(backup);
    const std::string saved{std::istreambuf_iterator<char>(backup), {}};
    CHECK_EQ(saved, broken);
}

static std::shared_ptr<providers::PluginProvider> make_plugin(const std::string& id, const std::string& on_search) {
    const fs::path dir = root / id;
    fs::create_directories(dir);
    write_file(dir / "plugin.json",
               "{\"id\":\"" + id + "\",\"api_version\":1,\"exec\":\"run.sh\",\"prefixes\":[\"\"]}");
    write_file(dir / "run.sh",
               "#!/bin/sh\necho '{\"type\":\"hello\",\"api_version\":1}'\nread -r line\n" + on_search + "\n");
    fs::permissions(dir / "run.sh", fs::perms::owner_exec, fs::perm_options::add);
    auto manifest = providers::plugin::parse_manifest(dir);
    assert(manifest);
    providers::PluginProvider::Options options;
    options.budget = std::chrono::milliseconds(1500);
    return std::make_shared<providers::PluginProvider>(*manifest, options);
}

// A plugin that dies, floods stdout or sends garbage yields no results; the other providers still answer.
static void misbehaving_plugins() {
    const std::pair<const char*, const char*> cases[] = {
        {"crash", "exit 1"},
        {"flood", "exec yes '{\"type\":\"x\"}'"},
        {"garbage", "echo 'not json'; sleep 1"},
    };
    for (const auto& [id, script] : cases) {
        auto plugin = make_plugin(id, script);
        const auto started = std::chrono::steady_clock::now();
        assert(plugin->search(core::Query("anything"), no_cancel).empty());
        assert(std::chrono::steady_clock::now() - started < std::chrono::seconds(5));

        core::SearchEngine engine;
        engine.register_provider(plugin);
        engine.register_provider(std::make_shared<providers::CalculatorProvider>());
        const auto results = engine.search(core::Query("1+1"));
        assert(!results.empty());
        CHECK_EQ(results[0].title, std::string("2"));
        plugin->request_shutdown();
        plugin->finish_shutdown(std::chrono::milliseconds(200));
    }
}

// With no gsettings on PATH the GNOME shortcut setup reports a clear failure and changes nothing.
static void missing_gsettings() {
    const char* path_env = std::getenv("PATH");
    const std::string old_path = path_env ? path_env : "";
    fs::create_directories(root / "empty-bin");
    setenv("PATH", (root / "empty-bin").c_str(), 1);

    const auto read = core::system_gsettings()({"get", "org.gnome.settings-daemon.plugins.media-keys", "x"});
    assert(!read.has_value());
    const auto result = core::setup_gnome_shortcut("Ctrl+Alt+K", "/usr/bin/nexus", core::system_gsettings());
    assert(!result.ok);
    assert(result.message.find("gsettings") != std::string::npos);

    setenv("PATH", old_path.c_str(), 1);
}

int main() {
    root = fs::temp_directory_path() / ("nexus_test_failure_" + std::to_string(getpid()));
    fs::remove_all(root);
    fs::create_directories(root);

    corrupt_database();
    unreadable_search_dir();
    malformed_config();
    misbehaving_plugins();
    missing_gsettings();

    fs::permissions(root / "locked", fs::perms::owner_all);
    fs::remove_all(root);
}
