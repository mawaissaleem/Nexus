#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include "nexus/core/search_engine.hpp"
#include "nexus/core/search_provider.hpp"
#include "nexus/providers/calc_provider.hpp"
#include "nexus/providers/shell_provider.hpp"
#include "nexus/utils/logger.hpp"

using namespace nexus::core;

// Minimal fake provider returning a single result
class FakeProvider : public ISearchProvider {
public:
    FakeProvider(std::string id, std::string name, int priority = 0, bool should_throw = false, size_t num_results = 1)
        : id_(std::move(id)), name_(std::move(name)), priority_(priority), should_throw_(should_throw), num_results_(num_results) {}

    std::string id() const override { return id_; }
    std::string name() const override { return name_; }
    int priority() const override { return priority_; }

    std::vector<SearchResult> search(const Query& /*query*/, const std::atomic<bool>& /*cancel_token*/) override {
        if (should_throw_) throw std::runtime_error("provider failure");
        std::vector<SearchResult> r;
        for (size_t i = 0; i < num_results_; ++i) {
            SearchResult s;
            s.id = id_ + "-" + std::to_string(i);
            s.title = name_ + " result " + std::to_string(i);
            s.provider_id = id_;
            s.score = 1.0 * (i + 1);
            r.push_back(std::move(s));
        }
        return r;
    }

private:
    std::string id_;
    std::string name_;
    int priority_;
    bool should_throw_;
    size_t num_results_;
};

// Records every query it receives; owns whatever prefixes it is given.
class PrefixProvider : public ISearchProvider {
public:
    PrefixProvider(std::string id, std::vector<std::string> prefixes) : id_(std::move(id)), prefixes_(std::move(prefixes)) {}
    std::string id() const override { return id_; }
    std::string name() const override { return id_; }
    std::vector<std::string> prefixes() const override { return prefixes_; }
    std::vector<SearchResult> search(const Query& q, const std::atomic<bool>&) override {
        seen.push_back(q.prefix() + "|" + q.text());
        SearchResult r;
        r.id = id_;
        r.title = id_;
        r.provider_id = id_;
        r.score = 1.0;
        return {r};
    }
    std::vector<std::string> seen;

private:
    std::string id_;
    std::vector<std::string> prefixes_;
};

using namespace std::chrono;
using ms = milliseconds;

// Concurrency probe: optionally sleeps, or blocks until released (and, if honour_cancel, until cancelled).
class GateProvider : public ISearchProvider {
public:
    GateProvider(std::string id, int priority, ms delay = ms(0), bool block = false, bool honour_cancel = true)
        : id_(std::move(id)), priority_(priority), delay_(delay), block_(block), honour_cancel_(honour_cancel) {}
    std::string id() const override { return id_; }
    std::string name() const override { return id_; }
    int priority() const override { return priority_; }
    std::vector<SearchResult> search(const Query&, const std::atomic<bool>& cancel) override {
        const int now_active = ++active;
        int seen = max_active.load();
        while (now_active > seen && !max_active.compare_exchange_weak(seen, now_active)) {}
        ++calls;
        std::this_thread::sleep_for(delay_);
        while (block_ && !released) {
            if (honour_cancel_ && cancel.load()) { saw_cancel = true; break; }
            std::this_thread::sleep_for(ms(2));
        }
        --active;
        SearchResult r;
        r.id = id_;
        r.title = id_;
        r.provider_id = id_;
        r.score = 1.0;
        finished = true;
        return {r};
    }
    std::atomic<int> calls{0}, active{0}, max_active{0};
    std::atomic<bool> released{false}, saw_cancel{false}, finished{false};

private:
    std::string id_;
    int priority_;
    ms delay_;
    bool block_, honour_cancel_;
};

template <class F> static bool wait_for(F cond, ms limit = ms(3000)) {
    for (auto end = steady_clock::now() + limit; steady_clock::now() < end; std::this_thread::sleep_for(ms(2)))
        if (cond()) return true;
    return false;
}

int main() {
    // duplicate provider id is rejected: the first registration stays, the second is not added (D-96)
    {
        SearchEngine engine;
        engine.register_provider(std::make_shared<FakeProvider>("dup", "First", 0, false, 1));
        engine.register_provider(std::make_shared<FakeProvider>("dup", "Second", 0, false, 3));
        assert(engine.providers().size() == 1);
        assert(engine.providers()[0]->name() == "First");
        assert(engine.search(Query("x")).size() == 1);
        engine.unregister_provider("dup");
        assert(engine.providers().empty());
    }

    // register_provider + search
    {
        SearchEngine engine;
        auto p = std::make_shared<FakeProvider>("p1", "P1", 0);
        engine.register_provider(p);
        Query q("hello");
        auto res = engine.search(q);
        assert(res.size() == 1);
        assert(res[0].provider_id == "p1");
    }

    // multiple providers -> both results present
    {
        SearchEngine engine;
        auto p1 = std::make_shared<FakeProvider>("p1", "P1", 0);
        auto p2 = std::make_shared<FakeProvider>("p2", "P2", 0);
        engine.register_provider(p1);
        engine.register_provider(p2);
        Query q("hello");
        auto res = engine.search(q);
        bool saw1 = false, saw2 = false;
        for (auto& r : res) {
            if (r.provider_id == "p1") saw1 = true;
            if (r.provider_id == "p2") saw2 = true;
        }
        assert(saw1 && saw2);
    }

    // unregister_provider
    {
        SearchEngine engine;
        auto p1 = std::make_shared<FakeProvider>("p1", "P1", 0);
        engine.register_provider(p1);
        engine.unregister_provider("p1");
        Query q("hello");
        auto res = engine.search(q);
        assert(res.empty());
    }

    // provider exception isolation
    {
        SearchEngine engine;
        auto bad = std::make_shared<FakeProvider>("bad", "Bad", 0, true);
        auto good = std::make_shared<FakeProvider>("good", "Good", 0);
        engine.register_provider(bad);
        engine.register_provider(good);
        Query q("hello");
        auto res = engine.search(q);
        // exception from bad provider should not prevent good provider result
        bool saw_good = false;
        for (auto& r : res) if (r.provider_id == "good") saw_good = true;
        assert(saw_good);
    }

    // max_results limiting
    {
        SearchEngine engine;
        // provider returns 5 results, request max_results = 2
        auto many = std::make_shared<FakeProvider>("many", "Many", 0, false, 5);
        engine.register_provider(many);
        Query q("hello");
        auto res = engine.search(q, 2);
        assert(res.size() <= 2);
    }

    // provider filter: disabled providers are skipped, and the filter is read per search (live)
    {
        SearchEngine engine;
        engine.register_provider(std::make_shared<FakeProvider>("a", "A", 0));
        engine.register_provider(std::make_shared<FakeProvider>("b", "B", 0));
        Query q("hello");
        assert(engine.search(q).size() == 2);  // no filter -> all enabled

        bool b_enabled = false;
        engine.set_provider_filter([&](const std::string& id) { return id != "b" || b_enabled; });
        auto res = engine.search(q);
        assert(res.size() == 1 && res[0].provider_id == "a");
        b_enabled = true;  // toggled without touching the engine
        assert(engine.search(q).size() == 2);
        engine.set_provider_filter(nullptr);
        assert(engine.search(q).size() == 2);

        engine.set_provider_filter([](const std::string&) { return false; });  // all off -> empty, no crash
        assert(engine.search(q).empty());
    }

    // max_results: different limits on the same engine apply per call
    {
        SearchEngine engine;
        engine.register_provider(std::make_shared<FakeProvider>("many", "Many", 0, false, 9));
        Query q("hello");
        assert(engine.search(q, 3).size() == 3);
        assert(engine.search(q, 8).size() == 8);
    }

    // Prefix routing: a prefixed query reaches only its owner (prefix stripped); unprefixed reaches "" claimers
    {
        SearchEngine engine;
        auto plain = std::make_shared<FakeProvider>("plain", "Plain");  // default prefixes() = {""}
        auto web = std::make_shared<PrefixProvider>("web", std::vector<std::string>{"web "});
        auto both = std::make_shared<PrefixProvider>("both", std::vector<std::string>{"both ", ""});
        engine.register_provider(plain);
        engine.register_provider(web);
        engine.register_provider(both);

        auto r = engine.search(Query("  web Robotics "));
        assert(r.size() == 1 && r[0].provider_id == "web");
        assert(web->seen.size() == 1 && web->seen[0] == "web |Robotics");

        r = engine.search(Query("both x"));
        assert(r.size() == 1 && r[0].provider_id == "both" && both->seen.back() == "both |x");

        r = engine.search(Query("hello"));
        assert(r.size() == 2);  // plain + both; web never sees it
        assert(web->seen.size() == 1 && both->seen.back() == "|hello");

        // Prefix words need their trailing space: "web" alone and "website" are unprefixed text
        assert(engine.search(Query("website")).size() == 2);
        assert(engine.search(Query("web")).size() == 2);
        assert(web->seen.size() == 1);

        // Unowned prefix ("clip x") is plain text for the general providers
        r = engine.search(Query("clip x"));
        assert(r.size() == 2 && both->seen.back() == "|clip x");

        // Disabled owner: its prefix still routes to it, so nothing leaks to the others
        engine.set_provider_filter([](const std::string& id) { return id != "web"; });
        assert(engine.search(Query("web x")).empty());
        engine.set_provider_filter(nullptr);

        // Unregistering frees the prefix
        engine.unregister_provider("web");
        assert(engine.search(Query("web x")).size() == 2);
    }

    // Longest prefix wins
    {
        SearchEngine engine;
        auto a = std::make_shared<PrefixProvider>("a", std::vector<std::string>{"x "});
        auto b = std::make_shared<PrefixProvider>("b", std::vector<std::string>{"x y "});
        engine.register_provider(a);
        engine.register_provider(b);
        auto r = engine.search(Query("x y z"));
        assert(r.size() == 1 && r[0].provider_id == "b" && b->seen[0] == "x y |z");
        r = engine.search(Query("x z"));
        assert(r.size() == 1 && r[0].provider_id == "a");
    }

    // Duplicate prefix: rejected with a warning, first owner keeps it, second still gets unprefixed queries
    {
        SearchEngine engine;
        auto first = std::make_shared<PrefixProvider>("first", std::vector<std::string>{"dup "});
        auto second = std::make_shared<PrefixProvider>("second", std::vector<std::string>{"dup ", ""});
        engine.register_provider(first);
        engine.register_provider(second);
        auto r = engine.search(Query("dup q"));
        assert(r.size() == 1 && r[0].provider_id == "first");
        assert(second->seen.empty());
        r = engine.search(Query("q"));
        assert(r.size() == 1 && r[0].provider_id == "second");
    }

    // Security: '>' reaches only ShellProvider, ShellProvider never answers unprefixed text,
    // and calculator works via "calc " and via plain arithmetic but not behind '>'
    {
        SearchEngine engine;
        engine.register_provider(std::make_shared<nexus::providers::ShellProvider>());
        engine.register_provider(std::make_shared<nexus::providers::CalculatorProvider>());
        auto other = std::make_shared<PrefixProvider>("other", std::vector<std::string>{""});
        engine.register_provider(other);

        auto r = engine.search(Query(">Echo hi"));
        assert(r.size() == 1 && r[0].provider_id == "shell" && r[0].action_kind == ActionKind::Shell);
        assert(r[0].execution_payload == "Echo hi");  // case kept
        assert(other->seen.empty());

        for (const char* plain : {"echo hi", "ls", "5 + 5", "shell", "calc 1+1"}) {
            for (const auto& res : engine.search(Query(plain))) assert(res.action_kind != ActionKind::Shell);
        }
        auto c = engine.search(Query("calc 2*3"));
        assert(c.size() == 1 && c[0].title == "6" && c[0].score <= 1000.0);
        bool plain_calc = false;
        for (const auto& res : engine.search(Query("2*3"))) plain_calc |= (res.provider_id == "calculator" && res.title == "6");
        assert(plain_calc);
        assert(engine.search(Query("> 2*3")).size() == 1);  // only the shell command, never a calc result
    }

    // Providers run concurrently: two 200 ms providers finish in well under 2 x 200 ms
    {
        SearchEngine engine(RankingEngine{}, ms(2000));
        engine.register_provider(std::make_shared<GateProvider>("a", 0, ms(200)));
        engine.register_provider(std::make_shared<GateProvider>("b", 0, ms(200)));
        const auto t0 = steady_clock::now();
        assert(engine.search(Query("x")).size() == 2);
        assert(steady_clock::now() - t0 < ms(380));
    }

    // Over-budget provider: dropped and logged, search returns without waiting, token reaches it,
    // its late result never appears, and a still-busy provider is not re-entered by the next search
    {
        SearchEngine engine(RankingEngine{}, ms(100));
        auto fast = std::make_shared<GateProvider>("fast", 0);
        auto stuck = std::make_shared<GateProvider>("stuck", 0, ms(0), true, /*honour_cancel=*/false);
        engine.register_provider(fast);
        engine.register_provider(stuck);

        std::ostringstream captured;
        auto* old = std::cerr.rdbuf(captured.rdbuf());
        const auto t0 = steady_clock::now();
        auto res = engine.search(Query("x"));
        const auto took = steady_clock::now() - t0;
        std::cerr.rdbuf(old);

        assert(took < ms(1500));  // returned on budget; the stuck provider is still blocked
        assert(!stuck->finished);
        assert(res.size() == 1 && res[0].provider_id == "fast");
        assert(captured.str().find("stuck") != std::string::npos && captured.str().find("budget") != std::string::npos);
        assert(captured.str().find("Provider fast") == std::string::npos);

        // Re-entrancy: stuck is still inside search(); the next search must not call it again.
        res = engine.search(Query("y"));
        assert(res.size() == 1 && stuck->calls == 1 && stuck->max_active == 1);

        // Once it returns, its late result is discarded and it can serve searches again.
        stuck->released = true;
        assert(wait_for([&] { return stuck->finished.load(); }));
        std::this_thread::sleep_for(ms(20));
        res = engine.search(Query("z"));
        assert(stuck->calls == 2);
        assert(res.size() == 2);
    }

    // The token of a search that ran over budget reaches the provider; so does the caller's token
    {
        SearchEngine engine(RankingEngine{}, ms(100));
        auto slow = std::make_shared<GateProvider>("slow", 0, ms(0), true);
        engine.register_provider(slow);
        assert(engine.search(Query("x")).empty());
        assert(wait_for([&] { return slow->saw_cancel.load(); }));
    }
    {
        SearchEngine engine(RankingEngine{}, ms(5000));
        auto slow = std::make_shared<GateProvider>("slow", 0, ms(0), true);
        engine.register_provider(slow);
        std::atomic<bool> token{false};
        std::vector<SearchResult> res;
        std::thread t([&] { res = engine.search(Query("x"), token); });
        assert(wait_for([&] { return slow->calls.load() == 1; }));
        token = true;
        t.join();  // returns long before the 5 s budget
        assert(res.empty());
        assert(wait_for([&] { return slow->saw_cancel.load(); }));
    }

    // Merge order follows registration order (priority), not completion order
    {
        for (const auto first_delay : {ms(0), ms(80)}) {
            SearchEngine engine(RankingEngine{}, ms(2000));
            engine.register_provider(std::make_shared<GateProvider>("hi", 10, first_delay));
            engine.register_provider(std::make_shared<GateProvider>("lo", 0, ms(80) - first_delay));
            auto res = engine.search(Query("x"));
            assert(res.size() == 2 && res[0].provider_id == "hi" && res[1].provider_id == "lo");
        }
    }

    // Shutdown with a provider still running neither crashes nor hangs beyond the pool's bounded wait
    {
        auto stuck = std::make_shared<GateProvider>("stuck", 0, ms(0), true, false);
        const auto t0 = steady_clock::now();
        {
            SearchEngine engine(RankingEngine{}, ms(50));
            engine.register_provider(stuck);
            assert(engine.search(Query("x")).empty());
        }
        assert(steady_clock::now() - t0 < ms(4000));
        stuck->released = true;  // the orphaned worker finishes against its own shared state
        assert(wait_for([&] { return stuck->finished.load(); }));
    }

    std::cout << "All SearchEngine tests passed successfully!\n";
    return 0;
}
