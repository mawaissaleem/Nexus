#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include "nexus/core/search_engine.hpp"
#include <atomic>
#include <cstdint>
#include <functional>
#include <condition_variable>
#include <optional>
#include <thread>
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
    struct Job {
        uint64_t generation{0};
        Query query;
        size_t max_results{0};
        CompletionCallback callback;
        std::shared_ptr<std::atomic<bool>> cancel_token;
    };
    void run();

    std::shared_ptr<SearchEngine> engine_;
    std::atomic<uint64_t> generation_{0};
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::optional<Job> pending_;  // only the latest query is kept; an older one that never started is dropped
    std::shared_ptr<std::atomic<bool>> latest_token_;  // token of the newest job, running or pending
    bool stop_{false};
    std::thread worker_;  // last member: starts after everything above exists
};

} // namespace nexus::core
