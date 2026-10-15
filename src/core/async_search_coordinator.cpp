#include "nexus/core/async_search_coordinator.hpp"
#include <stdexcept>

namespace nexus::core {

AsyncSearchCoordinator::AsyncSearchCoordinator(std::shared_ptr<SearchEngine> engine)
    : engine_(std::move(engine)) {
    if (!engine_) {
        throw std::invalid_argument("SearchEngine cannot be null");
    }
    worker_ = std::thread([this] { run(); });
}

AsyncSearchCoordinator::AsyncSearchCoordinator(SearchEngine& engine)
    : AsyncSearchCoordinator(std::shared_ptr<SearchEngine>(&engine, [](SearchEngine*) {})) {}

AsyncSearchCoordinator::~AsyncSearchCoordinator() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        if (latest_token_) latest_token_->store(true);
    }
    cv_.notify_all();
    worker_.join();  // engine->search returns promptly once its token is set
}

uint64_t AsyncSearchCoordinator::search(
    const Query& query,
    size_t max_results,
    CompletionCallback on_complete)
{
    auto token = std::make_shared<std::atomic<bool>>(false);
    uint64_t my_generation;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        my_generation = ++generation_;
        if (latest_token_) latest_token_->store(true);  // cancel the running/pending predecessor
        latest_token_ = token;
        pending_ = Job{my_generation, query, max_results, std::move(on_complete), std::move(token)};
    }
    cv_.notify_one();
    return my_generation;
}

void AsyncSearchCoordinator::cancel_all() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (latest_token_) latest_token_->store(true);
}

void AsyncSearchCoordinator::run() {
    std::unique_lock<std::mutex> lock(mutex_);
    for (;;) {
        cv_.wait(lock, [&] { return stop_ || pending_; });
        if (stop_) return;
        Job job = std::move(*pending_);
        pending_.reset();
        lock.unlock();

        if (!job.cancel_token->load()) {
            auto results = engine_->search(job.query, *job.cancel_token, job.max_results);
            // Discard results if a newer search has started or if this search was cancelled
            if (generation_.load() == job.generation && !job.cancel_token->load() && job.callback) {
                try {
                    job.callback(std::move(results), job.generation);
                } catch (...) {
                    // Suppress user callback exceptions in background thread
                }
            }
        }
        lock.lock();
    }
}

} // namespace nexus::core
