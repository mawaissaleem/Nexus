#include "nexus/core/ranking_engine.hpp"
#include "nexus/core/query.hpp"
#include <iostream>
#include <cassert>

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

    std::cout << "All RankingEngine tests passed successfully!\n";
    return 0;
}

