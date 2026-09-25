#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include "nexus/core/search_engine.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <vector>

namespace nexus::core {

class AsyncSearchCoordinator {
public:
    using CompletionCallback = std::function<void(std::vector<SearchResult>, uint64_t generation)>;

    explicit AsyncSearchCoordinator(std::shared_ptr<SearchEngine> engine);
    explicit AsyncSearchCoordinator(SearchEngine& engine);
    ~AsyncSearchCoordinator();

    AsyncSearchCoordinator(const AsyncSearchCoordinator&) = delete;
    AsyncSearchCoordinator& operator=(const AsyncSearchCoordinator&) = delete;

    uint64_t search(const Query& query, size_t max_results, CompletionCallback on_complete);
    uint64_t search(const Query& query, CompletionCallback on_complete) {
        return search(query, 20, std::move(on_complete));
    }

    void cancel_all();

    [[nodiscard]] uint64_t current_generation() const noexcept {
        return generation_.load();
    }

private:
    struct InFlightSearch {
        uint64_t generation{0};
        std::shared_ptr<std::atomic<bool>> cancel_token;
        std::future<void> future;
    };

    std::shared_ptr<SearchEngine> engine_;
    std::atomic<uint64_t> generation_{0};
    mutable std::mutex mutex_;
    std::vector<InFlightSearch> in_flight_;
};

} // namespace nexus::core

