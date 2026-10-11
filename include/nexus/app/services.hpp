#pragma once

#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/core/search_engine.hpp"
#include "nexus/index/app_indexer.hpp"
#include "nexus/index/database.hpp"
#include "nexus/index/file_indexer.hpp"
#include <memory>

namespace nexus::app {

// The database, indexers and search engine, shared by the CLI search commands and the GUI.
// Built only when a command needs them: `alias` and `dir` never get here.
struct Services {
    std::shared_ptr<index::Database> db;
    std::shared_ptr<index::AppIndexer> app_indexer;
    std::shared_ptr<index::FileIndexer> file_indexer;
    std::shared_ptr<core::SearchEngine> engine;
};

// `config` and `aliases` must outlive the returned services (providers hold references).
[[nodiscard]] Services make_services(core::ConfigManager& config, core::AliasManager& aliases);

// What the file indexer needs, read on the calling thread so worker threads never race the Settings dialog.
[[nodiscard]] index::FileIndexOptions file_index_options(const core::ConfigManager& config);

} // namespace nexus::app
