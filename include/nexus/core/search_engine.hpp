#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include "nexus/core/search_provider.hpp"
#include "nexus/core/ranking_engine.hpp"
#include <vector>
#include <memory>
#include <future>
#include <atomic>
#include <mutex>

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

    // Asynchronous search with cancellation support
    // Starts search in background, cancelling any in-flight search.
    std::future<std::vector<SearchResult>> search_async(const Query& query, size_t max_results = 20);

    // Cancel currently running searches
    void cancel();

    void set_ranking_engine(RankingEngine ranking_engine) { ranking_engine_ = std::move(ranking_engine); }
    [[nodiscard]] const RankingEngine& ranking_engine() const noexcept { return ranking_engine_; }

private:
    std::vector<SearchProviderPtr> providers_;
    RankingEngine ranking_engine_;
    std::atomic<bool> current_cancel_token_{false};
    std::mutex search_mutex_;
};

} // namespace nexus::core

