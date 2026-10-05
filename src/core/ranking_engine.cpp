#include "nexus/core/ranking_engine.hpp"
#include "nexus/utils/fuzzy.hpp"
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

        // 4. Usage boost from metadata (if available)
        double usage_score = 0.0;
        auto usage_it = res.metadata.find("query_usage_count");
        if (usage_it != res.metadata.end()) {
            try {
                int count = std::stoi(usage_it->second);
                if (count > 0) {
                    usage_score = std::log1p(count) * weights_.usage_boost;
                    // 5. Recency: exponential decay from the last launch for a similar query
                    auto last_it = res.metadata.find("query_last_used");
                    if (last_it != res.metadata.end() && weights_.recency_half_life_days > 0.0) {
                        const double age_days = std::max<std::int64_t>(0, now - std::stoll(last_it->second)) / 86400.0;
                        usage_score += weights_.recency_boost * std::exp2(-age_days / weights_.recency_half_life_days);
                    }
                }
            } catch (...) {
            }
        }

        // An exact title beats any non-exact one by at least exact_match - provider_relevance_weight
        // (lexical gap minus the largest provider-score difference); personalisation may use only half
        // of it, so no usage/recency value can close the gap (SRS 11). Unused when the gap is <= 0.
        usage_score = std::min(usage_score, 0.5 * std::max(0.0, weights_.exact_match - weights_.provider_relevance_weight));

        // Combine scores: normalize raw provider score into [0.0, 1.0], scale by provider_relevance_weight,
        // and add lexical and usage boosts
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

