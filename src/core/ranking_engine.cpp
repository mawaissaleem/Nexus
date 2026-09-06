#include "nexus/core/ranking_engine.hpp"
#include "nexus/utils/fuzzy.hpp"
#include <algorithm>
#include <cmath>

namespace nexus::core {

RankingEngine::RankingEngine(RankingWeights weights)
    : weights_(weights) {}

void RankingEngine::rank(const Query& query, std::vector<SearchResult>& results) const {
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
        auto usage_it = res.metadata.find("usage_count");
        if (usage_it != res.metadata.end()) {
            try {
                int count = std::stoi(usage_it->second);
                if (count > 0) {
                    usage_score = std::log1p(count) * weights_.usage_boost;
                }
            } catch (...) {
            }
        }

        // Combine scores with base fuzzy score
        res.score = (res.score * weights_.fuzzy_weight) + lexical_score + usage_score;
    }

    // Sort descending by score
    std::stable_sort(results.begin(), results.end(), [](const SearchResult& a, const SearchResult& b) {
        return a.score > b.score;
    });
}

} // namespace nexus::core

