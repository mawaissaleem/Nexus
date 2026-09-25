#include "nexus/core/async_search_coordinator.hpp"
#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace nexus::core {

AsyncSearchCoordinator::AsyncSearchCoordinator(std::shared_ptr<SearchEngine> engine)
    : engine_(std::move(engine)) {
    if (!engine_) {
        throw std::invalid_argument("SearchEngine cannot be null");
    }
}

AsyncSearchCoordinator::AsyncSearchCoordinator(SearchEngine& engine)
    : AsyncSearchCoordinator(std::shared_ptr<SearchEngine>(&engine, [](SearchEngine*) {})) {}

AsyncSearchCoordinator::~AsyncSearchCoordinator() {
    cancel_all();
    std::lock_guard<std::mutex> lock(mutex_);
    in_flight_.clear();
}

uint64_t AsyncSearchCoordinator::search(
    const Query& query,
    size_t max_results,
    CompletionCallback on_complete)
{
    const uint64_t my_generation = ++generation_;
    auto my_cancel_token = std::make_shared<std::atomic<bool>>(false);

    std::lock_guard<std::mutex> lock(mutex_);

    // 1. Cancel previous in-flight searches
    for (auto& item : in_flight_) {
        item.cancel_token->store(true);
    }

    // 2. Prune any finished tasks so in_flight_ stays bounded
    in_flight_.erase(
        std::remove_if(in_flight_.begin(), in_flight_.end(), [](auto& item) {
            return item.future.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
        }),
        in_flight_.end()
    );

    // 3. Dispatch the new search via std::async
    auto engine = engine_;
    auto fut = std::async(
        std::launch::async,
        [this, engine, query, max_results, my_cancel_token, my_generation, callback = std::move(on_complete)]() {
            auto results = engine->search(query, *my_cancel_token, max_results);

            // Discard results if a newer search has started or if this search was cancelled
            if (generation_.load() == my_generation && !my_cancel_token->load()) {
                if (callback) {
                    try {
                        callback(std::move(results), my_generation);
                    } catch (...) {
                        // Suppress user callback exceptions in background thread
                    }
                }
            }
        }
    );

    in_flight_.push_back(InFlightSearch{my_generation, my_cancel_token, std::move(fut)});

    return my_generation;
}

void AsyncSearchCoordinator::cancel_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : in_flight_) {
        item.cancel_token->store(true);
    }
}

} // namespace nexus::core

