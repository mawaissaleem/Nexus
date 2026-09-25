#include <cassert>
#include <chrono>
#include <iostream>
#include <memory>
#include <thread>
#include "nexus/core/async_search_coordinator.hpp"
#include "nexus/core/search_engine.hpp"
#include "nexus/core/search_provider.hpp"

using namespace nexus::core;

class FakeProvider : public ISearchProvider {
public:
    FakeProvider(std::string id, std::string name, int priority = 0)
        : id_(std::move(id)), name_(std::move(name)), priority_(priority) {}

    std::string id() const override { return id_; }
    std::string name() const override { return name_; }
    int priority() const override { return priority_; }

    void set_query_filter(std::string query) {
        query_filter_ = std::move(query);
    }

    void set_results(std::vector<SearchResult> results) {
        std::lock_guard<std::mutex> lock(mutex_);
        results_ = std::move(results);
    }

    void set_delay(std::chrono::milliseconds delay) {
        delay_ = delay;
    }

    bool was_cancelled() const {
        return was_cancelled_.load();
    }

    bool has_started() const {
        return has_started_.load();
    }

    std::vector<SearchResult> search(const Query& query, const std::atomic<bool>& cancel_token) override {
        if (!query_filter_.empty() && query.raw() != query_filter_) {
            return {};
        }
        has_started_.store(true);
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start) < delay_) {
            if (cancel_token.load()) {
                was_cancelled_.store(true);
                return {};
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if (cancel_token.load()) {
            was_cancelled_.store(true);
            return {};
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return results_;
    }

private:
    std::string id_;
    std::string name_;
    int priority_;
    std::string query_filter_;
    std::chrono::milliseconds delay_{0};
    mutable std::mutex mutex_;
    std::vector<SearchResult> results_;
    std::atomic<bool> was_cancelled_{false};
    std::atomic<bool> has_started_{false};
};

int main() {
    // 1. Basic delivery
    {
        auto engine = std::make_shared<SearchEngine>();
        auto provider = std::make_shared<FakeProvider>("p1", "P1");
        SearchResult r;
        r.id = "fixed-1";
        r.title = "Fixed 1";
        r.provider_id = "p1";
        provider->set_results({r});
        engine->register_provider(provider);

        AsyncSearchCoordinator coordinator(engine);

        std::promise<std::pair<std::vector<SearchResult>, uint64_t>> promise;
        auto fut = promise.get_future();

        uint64_t returned_gen = coordinator.search(Query("hello"), 10, [&](std::vector<SearchResult> res, uint64_t gen) {
            promise.set_value({std::move(res), gen});
        });

        assert(fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready);
        auto [res, gen] = fut.get();
        assert(gen == returned_gen);
        assert(res.size() == 1);
        assert(res[0].id == "fixed-1");
    }

    // 2. Superseded search is discarded
    {
        auto engine = std::make_shared<SearchEngine>();
        auto slow_provider = std::make_shared<FakeProvider>("slow", "Slow");
        slow_provider->set_delay(std::chrono::milliseconds(200));
        slow_provider->set_query_filter("slow_query");
        SearchResult r1;
        r1.id = "first";
        r1.title = "First";
        r1.provider_id = "slow";
        slow_provider->set_results({r1});
        engine->register_provider(slow_provider);

        AsyncSearchCoordinator coordinator(engine);

        std::atomic<int> callback_count{0};
        std::vector<SearchResult> last_results;
        uint64_t last_gen = 0;

        // Launch first (slow) search
        coordinator.search(Query("slow_query"), 10, [&](std::vector<SearchResult> res, uint64_t gen) {
            callback_count++;
            last_results = std::move(res);
            last_gen = gen;
        });

        // Add fast provider for second search
        auto fast_provider = std::make_shared<FakeProvider>("fast", "Fast");
        fast_provider->set_query_filter("fast_query");
        SearchResult r2;
        r2.id = "second";
        r2.title = "Second";
        r2.provider_id = "fast";
        fast_provider->set_results({r2});
        engine->register_provider(fast_provider);

        std::promise<void> second_done;
        auto second_fut = second_done.get_future();

        // Launch second (fast) search immediately without waiting
        uint64_t gen2 = coordinator.search(Query("fast_query"), 10, [&](std::vector<SearchResult> res, uint64_t gen) {
            callback_count++;
            last_results = std::move(res);
            last_gen = gen;
            second_done.set_value();
        });

        assert(second_fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready);

        // Wait long enough for the slow search's 200ms delay to finish
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // Callback must have been invoked exactly ONCE with "second" results
        assert(callback_count.load() == 1);
        assert(last_gen == gen2);
        assert(!last_results.empty());
        assert(last_results[0].id == "second");
    }

    // 3. Cancellation token is independently owned
    {
        auto engine = std::make_shared<SearchEngine>();
        auto slow_p = std::make_shared<FakeProvider>("slow", "Slow");
        slow_p->set_delay(std::chrono::milliseconds(300));
        engine->register_provider(slow_p);

        AsyncSearchCoordinator coordinator(engine);

        coordinator.search(Query("first"), 10, [](auto, auto) {});

        // Ensure the slow search has actually started running
        for (int i = 0; i < 100 && !slow_p->has_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(slow_p->has_started());
        assert(!slow_p->was_cancelled());

        // Start second search — must signal cancellation to the first search's token
        coordinator.search(Query("second"), 10, [](auto, auto) {});

        // Wait briefly for slow search to detect cancellation
        for (int i = 0; i < 100 && !slow_p->was_cancelled(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(slow_p->was_cancelled());
    }

    // 4. Generation numbers increase monotonically
    {
        auto engine = std::make_shared<SearchEngine>();
        AsyncSearchCoordinator coordinator(engine);

        uint64_t g1 = coordinator.search(Query("a"), 10, [](auto, auto) {});
        uint64_t g2 = coordinator.search(Query("b"), 10, [](auto, auto) {});
        uint64_t g3 = coordinator.search(Query("c"), 10, [](auto, auto) {});
        uint64_t g4 = coordinator.search(Query("d"), 10, [](auto, auto) {});
        uint64_t g5 = coordinator.search(Query("e"), 10, [](auto, auto) {});

        assert(g1 < g2);
        assert(g2 < g3);
        assert(g3 < g4);
        assert(g4 < g5);
        assert(coordinator.current_generation() == g5);
    }

    // 5. cancel_all() cancels in-flight search
    {
        auto engine = std::make_shared<SearchEngine>();
        auto slow_p = std::make_shared<FakeProvider>("slow", "Slow");
        slow_p->set_delay(std::chrono::milliseconds(300));
        engine->register_provider(slow_p);

        AsyncSearchCoordinator coordinator(engine);

        std::atomic<bool> callback_called{false};
        coordinator.search(Query("q"), 10, [&](auto, auto) {
            callback_called.store(true);
        });

        for (int i = 0; i < 100 && !slow_p->has_started(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(slow_p->has_started());

        coordinator.cancel_all();

        for (int i = 0; i < 100 && !slow_p->was_cancelled(); ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        assert(slow_p->was_cancelled());

        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        assert(!callback_called.load());
    }

    std::cout << "All AsyncSearchCoordinator tests passed successfully!\n";
    return 0;
}
