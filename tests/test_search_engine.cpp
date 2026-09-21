#include <cassert>
#include <iostream>
#include <memory>
#include "nexus/core/search_engine.hpp"
#include "nexus/core/search_provider.hpp"

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

    std::cout << "All SearchEngine tests passed successfully!\n";
    return 0;
}
