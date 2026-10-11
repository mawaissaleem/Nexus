#include "nexus/app/services.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/providers/app_provider.hpp"
#include "nexus/providers/calc_provider.hpp"
#include "nexus/providers/file_provider.hpp"
#include "nexus/providers/shell_provider.hpp"
#include "nexus/utils/logger.hpp"

namespace nexus::app {

Services make_services(core::ConfigManager& config, core::AliasManager& aliases) {
    Services s;
    s.db = std::make_shared<index::Database>();
    if (!s.db->open()) {
        NEXUS_LOG_WARN("Could not open local SQLite database. Running in-memory.");
    }
    s.app_indexer = std::make_shared<index::AppIndexer>(s.db);
    s.app_indexer->initialize();
    s.file_indexer = std::make_shared<index::FileIndexer>(s.db);

    s.engine = std::make_shared<core::SearchEngine>();
    s.engine->register_provider(std::make_shared<providers::AliasProvider>(aliases));
    s.engine->register_provider(std::make_shared<providers::ApplicationProvider>(
        s.app_indexer, s.db, [&config] { return config.is_history_enabled(); }));
    s.engine->register_provider(std::make_shared<providers::FileProvider>(s.db));
    s.engine->register_provider(std::make_shared<providers::CalculatorProvider>());
    s.engine->register_provider(std::make_shared<providers::ShellProvider>());

    // Providers switched off in Settings are skipped per search, so the toggle needs no restart.
    s.engine->set_provider_filter([&config](const std::string& id) { return config.is_provider_enabled(id); });
    return s;
}

index::FileIndexOptions file_index_options(const core::ConfigManager& config) {
    return index::FileIndexOptions{config.get_search_directories(), config.get_file_exclusions()};
}

} // namespace nexus::app
