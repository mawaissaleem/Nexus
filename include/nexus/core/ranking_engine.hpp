#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include <vector>

namespace nexus::core {

struct RankingWeights {
    double exact_match{500.0};
    double prefix_match{250.0};
    double fuzzy_weight{1.0};
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

