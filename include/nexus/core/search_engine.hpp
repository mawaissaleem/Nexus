#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include "nexus/core/search_provider.hpp"
#include "nexus/core/ranking_engine.hpp"
#include <vector>
#include <memory>
#include <atomic>
#include <mutex>
#include <functional>
#include <string>
#include <map>

namespace nexus::core {

class SearchEngine {
public:
    explicit SearchEngine(RankingEngine ranking_engine = RankingEngine{});
    ~SearchEngine();

    void register_provider(SearchProviderPtr provider);
    void unregister_provider(const std::string& provider_id);
    [[nodiscard]] const std::vector<SearchProviderPtr>& providers() const noexcept { return providers_; }

    // Synchronous search across all providers with ranking
    std::vector<SearchResult> search(const Query& query, size_t max_results = 20);
    std::vector<SearchResult> search(const Query& query, std::atomic<bool>& cancel_token, size_t max_results = 20);

    // Providers for which `is_enabled(id)` is false are skipped on every search (read per search, so
    // changes apply live). Empty function = all enabled.
    void set_provider_filter(std::function<bool(const std::string&)> is_enabled);

    void set_ranking_engine(RankingEngine ranking_engine) { ranking_engine_ = std::move(ranking_engine); }
    [[nodiscard]] const RankingEngine& ranking_engine() const noexcept { return ranking_engine_; }

private:
    std::vector<SearchProviderPtr> providers_;
    std::map<std::string, std::string> prefix_owner_;  // non-empty prefix -> owning provider id
    RankingEngine ranking_engine_;
    std::function<bool(const std::string&)> provider_filter_;
    std::mutex search_mutex_;
};

} // namespace nexus::core

