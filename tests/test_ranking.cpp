#include "nexus/core/ranking_engine.hpp"
#include "nexus/core/query.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

int main() {
    using namespace nexus::core;

    RankingEngine ranker;

    Query q("term");

    SearchResult exact;
    exact.id = "exact";
    exact.title = "term";
    exact.score = 50.0;

    SearchResult prefix;
    prefix.id = "prefix";
    prefix.title = "terminal";
    prefix.score = 50.0;

    SearchResult fuzzy;
    fuzzy.id = "fuzzy";
    fuzzy.title = "pattern matcher";
    fuzzy.score = 50.0;

    std::vector<SearchResult> results = {fuzzy, prefix, exact};
    ranker.rank(q, results);

    assert(results.size() == 3);
    // exact should be #1, prefix #2, fuzzy #3
    assert(results[0].id == "exact");
    assert(results[1].id == "prefix");
    assert(results[2].id == "fuzzy");

    // Test usage boost
    SearchResult low_usage;
    low_usage.id = "low";
    low_usage.title = "app low";
    low_usage.score = 50.0;

    SearchResult high_usage;
    high_usage.id = "high";
    high_usage.title = "app high";
    high_usage.score = 50.0;
    high_usage.metadata["usage_count"] = "50";

    Query app_q("app");
    std::vector<SearchResult> usage_results = {low_usage, high_usage};
    ranker.rank(app_q, usage_results);

    assert(usage_results[0].id == "high");

    // Test 3: Provider score no longer silently dominates lexical relevance (D-29)
    {
        Query q_calc("calculator");

        SearchResult high_provider_poor_lexical;
        high_provider_poor_lexical.id = "high_provider_poor_lexical";
        high_provider_poor_lexical.title = "Unrelated Tool";
        high_provider_poor_lexical.score = 2500.0;

        SearchResult low_provider_exact_lexical;
        low_provider_exact_lexical.id = "low_provider_exact_lexical";
        low_provider_exact_lexical.title = "calculator";
        low_provider_exact_lexical.score = 400.0;

        std::vector<SearchResult> dom_results = {high_provider_poor_lexical, low_provider_exact_lexical};
        ranker.rank(q_calc, dom_results);

        assert(dom_results.size() == 2);
        assert(dom_results[0].id == "low_provider_exact_lexical");
        assert(dom_results[1].id == "high_provider_poor_lexical");
        assert(dom_results[0].score > dom_results[1].score);
    }

    // Test 4: Normalization clamping (scores above max_provider_score or below 0)
    {
        Query q_clamp("xyz");

        SearchResult max_score_res;
        max_score_res.id = "max_score";
        max_score_res.title = "item alpha";
        max_score_res.score = 2500.0;

        SearchResult excessive_score_res;
        excessive_score_res.id = "excessive_score";
        excessive_score_res.title = "item beta";
        excessive_score_res.score = 5000.0;

        std::vector<SearchResult> clamp_results = {max_score_res, excessive_score_res};
        ranker.rank(q_clamp, clamp_results);

        assert(clamp_results.size() == 2);
        // Both items have identical lexical score (0 for query "xyz") and clamped relevance contribution (400.0)
        assert(std::abs(clamp_results[0].score - clamp_results[1].score) < 1e-6);
        assert(std::abs(clamp_results[0].score - 400.0) < 1e-6);

        // Lower bound clamp: negative score clamps to 0.0
        SearchResult negative_score_res;
        negative_score_res.id = "negative_score";
        negative_score_res.title = "item gamma";
        negative_score_res.score = -100.0;

        std::vector<SearchResult> neg_results = {negative_score_res};
        ranker.rank(q_clamp, neg_results);
        assert(std::abs(neg_results[0].score - 0.0) < 1e-6);
    }

    std::cout << "All RankingEngine tests passed successfully!\n";
    return 0;
}

