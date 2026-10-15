#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include "nexus/core/search_provider.hpp"
#include "nexus/core/ranking_engine.hpp"
#include <chrono>
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
    // Providers run concurrently on a small fixed pool. One whose results are not in within `provider_budget`
    // of the search start is dropped from that search (logged, cancel token set) and never waited on.
    // A cancelled search returns once its providers have returned (or the budget ends), so only an over-budget
    // provider can still be running at the next search; it is skipped, so search() is never re-entered per provider.
    static constexpr std::chrono::milliseconds kDefaultProviderBudget{200};
    explicit SearchEngine(RankingEngine ranking_engine = RankingEngine{},
                          std::chrono::milliseconds provider_budget = kDefaultProviderBudget);
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
    class Pool;
    std::vector<SearchProviderPtr> providers_;
    std::map<std::string, std::shared_ptr<std::atomic<bool>>> busy_;  // provider id -> a search() call is running
    std::chrono::milliseconds provider_budget_;
    std::map<std::string, std::string> prefix_owner_;  // non-empty prefix -> owning provider id
    RankingEngine ranking_engine_;
    std::function<bool(const std::string&)> provider_filter_;
    std::mutex search_mutex_;
    std::unique_ptr<Pool> pool_;  // last member: destroyed first
};

} // namespace nexus::core

