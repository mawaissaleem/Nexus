#include "nexus/core/ranking_engine.hpp"
#include "nexus/core/query.hpp"
#include "nexus/providers/app_provider.hpp"
#include <iostream>
#include <cassert>
#include <cmath>
#include <algorithm>
#include <limits>
#include <sstream>
#include "nexus/utils/logger.hpp"
#include <string>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <unistd.h>

// P5.1: provider output for `q` (the two test apps only), ranked.
static std::vector<nexus::core::SearchResult> ranked_pair(nexus::providers::ApplicationProvider& provider,
                                                          const nexus::core::RankingEngine& ranker, const std::string& q) {
    std::atomic<bool> cancel{false};
    std::vector<nexus::core::SearchResult> out;
    for (auto& r : provider.search(nexus::core::Query(q), cancel)) out.push_back(std::move(r));
    assert(out.size() == 2);
    ranker.rank(nexus::core::Query(q), out);
    return out;
}

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
    high_usage.usage_count = 50;

    Query app_q("app");
    std::vector<SearchResult> usage_results = {low_usage, high_usage};
    ranker.rank(app_q, usage_results);

    assert(usage_results[0].id == "high");

    // Recency decay (injected clock): same count, the more recently used wins; boost halves per half-life
    {
        const std::int64_t now = 1'000'000'000;
        const std::int64_t day = 86400;
        auto make = [&](const char* id, std::int64_t age_days) {
            SearchResult r;
            r.id = id;
            r.title = "app";
            r.score = 50.0;
            r.usage_count = 3;
            r.last_used = now - age_days * day;
            return r;
        };
        std::vector<SearchResult> rs = {make("old", 60), make("fresh", 0), make("week", 7)};
        ranker.rank(app_q, rs, now);
        assert(rs[0].id == "fresh" && rs[1].id == "week" && rs[2].id == "old");
        const double w = ranker.get_weights().recency_boost;
        assert(std::abs((rs[0].score - rs[1].score) - w / 2) < 1e-9);  // 7 days = one half-life
        // Without a timestamp there is no recency term; a future timestamp (clock skew) is clamped to "now"
        SearchResult none = make("none", 0);
        none.last_used = 0;
        SearchResult future = make("future", -5);
        std::vector<SearchResult> rs2 = {none, future};
        ranker.rank(app_q, rs2, now);
        assert(std::abs((rs2[0].score - rs2[1].score) - w) < 1e-9 && rs2[0].id == "future");
        // Zero usage count: recency alone adds nothing
        SearchResult zero = make("zero", 0);
        zero.usage_count = 0;
        SearchResult bare = make("bare", 0);
        bare.usage_count = 0; bare.last_used = 0;
        std::vector<SearchResult> rs3 = {zero, bare};
        ranker.rank(app_q, rs3, now);
        assert(rs3[0].score == rs3[1].score);
    }

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

    // Test 5 (P5.1): launches from "co" boost X for "co"/"cod" but leave "fi" untouched.
    {
        namespace fs = std::filesystem;
        const fs::path root = fs::temp_directory_path() / ("nexus_ranking_" + std::to_string(getpid()));
        fs::remove_all(root);
        const fs::path dir = root / "sys" / "applications";
        fs::create_directories(dir);
        // Both titles match "co", "cod" and "fi"; with no usage Y ranks above X for "co" and "cod".
        std::ofstream(dir / "x_app.desktop") << "[Desktop Entry]\nType=Application\nName=Codex Fiddler\nExec=x_app\n";
        std::ofstream(dir / "y_app.desktop") << "[Desktop Entry]\nType=Application\nName=Cod Fi\nExec=y_app\n";

        auto db = std::make_shared<nexus::index::Database>((root / "nexus.db").string());
        assert(db->open());
        auto indexer = std::make_shared<nexus::index::AppIndexer>(db, std::vector<fs::path>{dir});
        assert(indexer->rebuild_index() == 2);
        nexus::providers::ApplicationProvider provider(indexer, db);

        assert(ranked_pair(provider, ranker, "co")[0].id == "y_app");
        assert(ranked_pair(provider, ranker, "cod")[0].id == "y_app");
        const auto fi_before = ranked_pair(provider, ranker, "fi");

        for (int i = 0; i < 5; ++i) assert(db->record_usage("x_app", "co"));

        assert(ranked_pair(provider, ranker, "co")[0].id == "x_app");
        assert(ranked_pair(provider, ranker, "cod")[0].id == "x_app");
        // A global usage count would raise X's score here; a query-conditioned one must not.
        const auto fi_after = ranked_pair(provider, ranker, "fi");
        assert(fi_after[0].id == fi_before[0].id && fi_after[1].id == fi_before[1].id);
        assert(std::abs(fi_after[0].score - fi_before[0].score) < 1e-9 && std::abs(fi_after[1].score - fi_before[1].score) < 1e-9);

        db->close();
        fs::remove_all(root);
    }

    // Test 6 (P5.3): personalisation is capped below the exact-match gap (SRS 11).
    {
        const RankingWeights w;
        const double cap = 0.5 * (w.exact_match - w.provider_relevance_weight);
        auto mk = [](const char* id, const char* title, double score, int count) {
            SearchResult r;
            r.id = id; r.title = title; r.score = score;
            if (count) { r.usage_count = count; r.last_used = 1000; }
            return r;
        };
        // Worst case: exact title with minimum provider score and no usage vs a non-exact with the
        // maximum provider score, a prefix match and maximal usage/recency.
        std::vector<SearchResult> v = {mk("rival", "terminal", w.max_provider_score, 2000000000), mk("exact", "term", 0.0, 0)};
        ranker.rank(Query("term"), v, 1000);
        assert(v[0].id == "exact");
        const double rival_base = w.provider_relevance_weight + w.prefix_match + w.token_match;
        assert(std::abs(v[1].score - (rival_base + cap)) < 1e-9);   // personalisation saturates exactly at the cap

        // Pathological weights (huge usage_boost) still cannot outrank.
        RankingWeights big; big.usage_boost = 1e12;
        v = {mk("rival", "terminal", big.max_provider_score, 2000000000), mk("exact", "term", 0.0, 0)};
        RankingEngine(big).rank(Query("term"), v, 1000);
        assert(v[0].id == "exact");

        // Below the cap personalisation is untouched: 1 launch just now = 5*ln2 + 2.
        v = {mk("a", "x", 0.0, 1)};
        ranker.rank(Query("zzz"), v, 1000);
        assert(std::abs(v[0].score - (5.0 * std::log(2.0) + 2.0)) < 1e-9);

        // Gap <= 0 disables personalisation instead of going negative.
        RankingWeights flat; flat.exact_match = 100.0; flat.provider_relevance_weight = 400.0;
        v = {mk("a", "x", 0.0, 5)};
        RankingEngine(flat).rank(Query("zzz"), v, 1000);
        assert(v[0].score == 0.0);
    }

    // PA.4 / D-80: over-limit scores are logged; NaN/inf are treated as 0 and the sort stays valid.
    {
        std::ostringstream captured;
        auto* old = std::cerr.rdbuf(captured.rdbuf());
        const auto saved_level = nexus::utils::Logger::instance().get_level();
        nexus::utils::Logger::instance().set_level(nexus::utils::LogLevel::Warning);

        auto mk = [](const char* id, double score) {
            SearchResult r; r.id = id; r.title = "item"; r.score = score; return r;
        };
        std::vector<SearchResult> ok = {mk("ok", 2500.0)};
        ranker.rank(Query("zzz"), ok);
        assert(captured.str().empty());                       // at the limit: no warning
        std::vector<SearchResult> over = {mk("over", 2500.5)};
        ranker.rank(Query("zzz"), over);
        assert(captured.str().find("exceeds max_provider_score") != std::string::npos);
        assert(std::abs(over[0].score - 400.0) < 1e-9);       // still clamped

        captured.str("");
        const double nan = std::numeric_limits<double>::quiet_NaN(), inf = std::numeric_limits<double>::infinity();
        std::vector<SearchResult> bad = {mk("nan", nan), mk("good", 1250.0), mk("pinf", inf), mk("ninf", -inf), mk("good2", 1250.0)};
        ranker.rank(Query("zzz"), bad);
        assert(captured.str().find("non-finite") != std::string::npos);
        for (const auto& r : bad) assert(std::isfinite(r.score));
        assert(bad[0].id == "good" && bad[1].id == "good2");  // finite 200 beats the zeroed ones
        assert(bad[2].score == 0.0 && bad[3].score == 0.0 && bad[4].score == 0.0);
        assert(std::is_sorted(bad.begin(), bad.end(), [](const SearchResult& a, const SearchResult& b) { return a.score > b.score; }));

        std::cerr.rdbuf(old);
        nexus::utils::Logger::instance().set_level(saved_level);
    }

    std::cout << "All RankingEngine tests passed successfully!\n";
    return 0;
}

