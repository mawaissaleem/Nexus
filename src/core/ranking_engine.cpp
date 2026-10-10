#include "nexus/core/ranking_engine.hpp"
#include "nexus/utils/fuzzy.hpp"
#include "nexus/utils/logger.hpp"
#include <algorithm>
#include <cmath>

namespace nexus::core {

RankingEngine::RankingEngine(RankingWeights weights)
    : weights_(weights) {}

void RankingEngine::rank(const Query& query, std::vector<SearchResult>& results, std::int64_t now) const {
    const std::string& query_norm = query.normalized();

    for (auto& res : results) {
        double lexical_score = 0.0;
        std::string title_norm = utils::FuzzyMatcher::to_lower(res.title);

        // 1. Exact match boost
        if (title_norm == query_norm) {
            lexical_score += weights_.exact_match;
        }

        // 2. Prefix match boost
        if (utils::FuzzyMatcher::istarts_with(title_norm, query_norm)) {
            lexical_score += weights_.prefix_match;
        }

        // 3. Token match boost
        for (const auto& token : query.tokens()) {
            if (utils::FuzzyMatcher::icontains(title_norm, token)) {
                lexical_score += weights_.token_match;
            }
        }

        // 4. Usage boost; 5. recency: exponential decay from the last launch for a similar query
        double usage_score = 0.0;
        if (res.usage_count > 0) {
            usage_score = std::log1p(res.usage_count) * weights_.usage_boost;
            if (res.last_used > 0 && weights_.recency_half_life_days > 0.0) {
                const double age_days = std::max<std::int64_t>(0, now - res.last_used) / 86400.0;
                usage_score += weights_.recency_boost * std::exp2(-age_days / weights_.recency_half_life_days);
            }
        }

        // An exact title beats any non-exact one by at least exact_match - provider_relevance_weight
        // (lexical gap minus the largest provider-score difference); personalisation may use only half
        // of it, so no usage/recency value can close the gap (SRS 11). Unused when the gap is <= 0.
        usage_score = std::min(usage_score, 0.5 * std::max(0.0, weights_.exact_match - weights_.provider_relevance_weight));

        // Combine scores: normalize raw provider score into [0.0, 1.0], scale by provider_relevance_weight,
        // and add lexical and usage boosts
        // NaN/inf would break the sort's strict weak ordering (std::clamp passes NaN through): treat as 0.
        if (!std::isfinite(res.score)) {
            NEXUS_LOG_WARN("RankingEngine: non-finite provider score for '" + res.id + "' treated as 0");
            res.score = 0.0;
        } else if (res.score > weights_.max_provider_score && weights_.max_provider_score > 0.0) {
            NEXUS_LOG_WARN("RankingEngine: provider score " + std::to_string(res.score) + " for '" + res.id +
                           "' exceeds max_provider_score; clamped");
        }
        double normalized_relevance = (weights_.max_provider_score > 0.0)
            ? std::clamp(res.score / weights_.max_provider_score, 0.0, 1.0)
            : 0.0;
        res.score = (normalized_relevance * weights_.provider_relevance_weight) + lexical_score + usage_score;
    }

    // Sort descending by score
    std::stable_sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
        return a.score > b.score;
    });
}

} // namespace nexus::core

