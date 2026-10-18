#include "nexus/app/services.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/providers/app_provider.hpp"
#include "nexus/providers/calc_provider.hpp"
#include "nexus/providers/file_provider.hpp"
#include "nexus/providers/shell_provider.hpp"
#include "nexus/providers/plugin_loader.hpp"
#include "nexus/utils/logger.hpp"
#include <algorithm>
#include <set>

namespace nexus::app {

Services make_services(core::ConfigManager& config, core::AliasManager& aliases,
                       const std::vector<std::filesystem::path>& plugin_dirs) {
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

    // Built-in ids, plus the names the Settings UI uses, can never be taken by a plugin.
    std::set<std::string> reserved{"settings", "aliases"};
    for (const auto& p : s.engine->providers()) reserved.insert(p->id());
    for (auto& manifest : providers::discover_plugins(plugin_dirs, reserved)) {
        auto plugin = std::make_shared<providers::PluginProvider>(std::move(manifest));
        s.plugins.push_back(plugin);
        s.engine->register_provider(plugin);
    }

    // Providers switched off in Settings are skipped per search, so the toggle needs no restart.
    s.engine->set_provider_filter([&config](const std::string& id) { return config.is_provider_enabled(id); });
    return s;
}

void shutdown_services(Services& s, std::chrono::milliseconds grace) {
    for (auto& p : s.plugins) p->request_shutdown();
    const auto until = std::chrono::steady_clock::now() + grace;
    for (auto& p : s.plugins) {
        p->finish_shutdown(std::max(std::chrono::milliseconds(0), std::chrono::duration_cast<std::chrono::milliseconds>(
                                                                      until - std::chrono::steady_clock::now())));
    }
}

index::FileIndexOptions file_index_options(const core::ConfigManager& config) {
    return index::FileIndexOptions{config.get_search_directories(), config.get_file_exclusions()};
}

} // namespace nexus::app
