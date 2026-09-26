#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include <vector>

namespace nexus::core {

struct RankingWeights {
    // The highest raw score any provider is expected to produce. Used to
    // normalize res.score into a 0.0–1.0 "provider relevance" value before
    // combining it with this engine's own lexical/usage signals below, so
    // that changing a provider's raw score scale and changing these
    // weights are two independent decisions, not one entangled one.
    // Current known maximum across providers is AliasProvider's 2500.0
    // (offering to create a new alias) — verify this is still the highest
    // value across all providers before picking this constant, and update
    // this comment if it changes.
    double max_provider_score{2500.0};

    double exact_match{500.0};
    double prefix_match{250.0};
    double provider_relevance_weight{400.0};
    double token_match{50.0};
    double usage_boost{5.0};
    double recency_boost{2.0};
};

class RankingEngine {
public:
    explicit RankingEngine(RankingWeights weights = RankingWeights{});

    // Re-score and sort results in-place in descending score order
    void rank(const Query& query, std::vector<SearchResult>& results) const;

    void set_weights(const RankingWeights& weights) { weights_ = weights; }
    [[nodiscard]] const RankingWeights& get_weights() const noexcept { return weights_; }

private:
    RankingWeights weights_;
};

} // namespace nexus::core

