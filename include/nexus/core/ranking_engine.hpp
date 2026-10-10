#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include <cstdint>
#include <ctime>
#include <vector>

namespace nexus::core {

struct RankingWeights {
    // The highest raw score any provider is expected to produce. Used to
    // normalize res.score into a 0.0–1.0 "provider relevance" value before
    // combining it with this engine's own lexical/usage signals below, so
    // that changing a provider's raw score scale and changing these
    // weights are two independent decisions, not one entangled one.
    // Current known maximum across providers is AliasProvider's 2500.0
    // (offering to create a new alias). rank() logs a warning for any
    // score above this, so a provider that outgrows it is noticed.
    double max_provider_score{2500.0};

    double exact_match{500.0};
    double prefix_match{250.0};
    double provider_relevance_weight{400.0};
    double token_match{50.0};
    double usage_boost{5.0};
    // Added on top of the usage boost for apps launched from a similar query: recency_boost at
    // "just used", halving every recency_half_life_days.
    double recency_boost{2.0};
    double recency_half_life_days{7.0};
};

class RankingEngine {
public:
    explicit RankingEngine(RankingWeights weights = RankingWeights{});

    // Re-score and sort results in-place in descending score order
    // `now` (Unix seconds) is a parameter so recency decay is testable without the wall clock.
    void rank(const Query& query, std::vector<SearchResult>& results,
              std::int64_t now = static_cast<std::int64_t>(std::time(nullptr))) const;

    void set_weights(const RankingWeights& weights) { weights_ = weights; }
    [[nodiscard]] const RankingWeights& get_weights() const noexcept { return weights_; }

private:
    RankingWeights weights_;
};

} // namespace nexus::core

