#include <cassert>
#include <iostream>
#include <memory>
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

int main() {
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

    std::cout << "All SearchEngine tests passed successfully!\n";
    return 0;
}
