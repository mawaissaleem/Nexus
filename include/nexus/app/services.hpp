#pragma once

#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/core/search_engine.hpp"
#include "nexus/index/app_indexer.hpp"
#include "nexus/index/database.hpp"
#include "nexus/index/file_indexer.hpp"
#include "nexus/providers/plugin_loader.hpp"
#include "nexus/providers/plugin_provider.hpp"
#include <filesystem>
#include <memory>
#include <vector>

namespace nexus::app {

// The database, indexers and search engine, shared by the CLI search commands and the GUI.
// Built only when a command needs them: `alias` and `dir` never get here.
struct Services {
    std::shared_ptr<index::Database> db;
    std::shared_ptr<index::AppIndexer> app_indexer;
    std::shared_ptr<index::FileIndexer> file_indexer;
    std::shared_ptr<core::SearchEngine> engine;
    std::vector<std::shared_ptr<providers::PluginProvider>> plugins;  // also registered in `engine`; nothing spawned until a search
};

// `config` and `aliases` must outlive the returned services (providers hold references).
// Plugins are discovered in `plugin_dirs` (default: the XDG plugin directories, user dir first).
[[nodiscard]] Services make_services(core::ConfigManager& config, core::AliasManager& aliases,
                                     const std::vector<std::filesystem::path>& plugin_dirs = providers::plugin_search_dirs());

// Asks every plugin to exit and waits one shared `grace` for them, then kills the rest. Call it explicitly on
// teardown (not from a destructor); safe to call twice.
void shutdown_services(Services& s, std::chrono::milliseconds grace = std::chrono::milliseconds(500));

// What the file indexer needs, read on the calling thread so worker threads never race the Settings dialog.
[[nodiscard]] index::FileIndexOptions file_index_options(const core::ConfigManager& config);

} // namespace nexus::app
