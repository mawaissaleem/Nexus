#include "nexus/core/search_engine.hpp"
#include "nexus/utils/logger.hpp"
#include <algorithm>
#include <condition_variable>
#include <deque>
#include <optional>
#include <thread>

namespace nexus::core {

// Fixed set of detached workers that share only `State`, so a worker stuck in a provider can outlive the
// engine without touching it. The destructor waits a bounded time for workers to finish, then lets go.
// Provider contract: an abandoned (over-budget) task can outlive the engine by up to that 1 s bound, so a
// provider must not reference objects that may die before its search() returns (PluginProvider owns all it touches and returns {} after shutdown).
class SearchEngine::Pool {
public:
    explicit Pool(size_t threads) : state_(std::make_shared<State>()) {
        for (size_t i = 0; i < threads; ++i) {
            ++state_->alive;
            std::thread([s = state_] { run(*s); }).detach();
        }
    }
    ~Pool() {
        std::unique_lock<std::mutex> lock(state_->m);
        state_->stop = true;
        state_->cv.notify_all();
        state_->cv.wait_for(lock, std::chrono::seconds(1), [&] { return state_->alive == 0; });
    }
    void submit(std::function<void()> task) {
        std::lock_guard<std::mutex> lock(state_->m);
        state_->tasks.push_back(std::move(task));
        state_->cv.notify_one();
    }

private:
    struct State {
        std::mutex m;
        std::condition_variable cv;
        std::deque<std::function<void()>> tasks;
        bool stop{false};
        size_t alive{0};
    };
    static void run(State& s) {
        std::unique_lock<std::mutex> lock(s.m);
        for (;;) {
            s.cv.wait(lock, [&] { return s.stop || !s.tasks.empty(); });
            if (s.stop) break;
            auto task = std::move(s.tasks.front());
            s.tasks.pop_front();
            lock.unlock();
            task();
            lock.lock();
        }
        --s.alive;
        s.cv.notify_all();  // state_ is kept alive by the caller's shared_ptr copy
    }
    std::shared_ptr<State> state_;
};

namespace {

// Everything a provider task touches lives here, shared with the pool, so an abandoned (over-budget)
// task can finish safely after search() has returned.
struct Batch {
    explicit Batch(Query q) : query(std::move(q)) {}
    Query query;
    std::atomic<bool> cancel{false};  // handed to providers; set on caller cancel or budget overrun
    std::mutex m;
    std::condition_variable cv;
    size_t pending{0};
    bool closed{false};  // search() stopped waiting; late results are discarded
    std::vector<std::optional<std::vector<SearchResult>>> slots;  // by provider order, not completion order
    std::vector<char> done;                                        // provider returned (or threw) in time
};

} // namespace

SearchEngine::SearchEngine(RankingEngine ranking_engine, std::chrono::milliseconds provider_budget)
    : provider_budget_(provider_budget),
      ranking_engine_(std::move(ranking_engine)),
      pool_(std::make_unique<Pool>(std::clamp(std::thread::hardware_concurrency(), 2u, 8u))) {}

SearchEngine::~SearchEngine() = default;

void SearchEngine::register_provider(SearchProviderPtr provider) {
    if (!provider) return;
    std::lock_guard<std::mutex> lock(search_mutex_);
    // Routing, busy flags and unregister all key on the id, so a second provider with it would be ambiguous (D-96).
    for (const auto& p : providers_) {
        if (p->id() == provider->id()) {
            NEXUS_LOG_WARN("Provider id '" + provider->id() + "' already registered, duplicate rejected");
            return;
        }
    }
    // A rejected claimant of a prefix does not inherit it if the owner is unregistered later; re-register it then.
    for (const auto& prefix : provider->prefixes()) {
        if (prefix.empty()) continue;  // "" = unprefixed queries, shared by any number of providers
        const auto [it, inserted] = prefix_owner_.emplace(prefix, provider->id());
        if (!inserted) {
            NEXUS_LOG_WARN("Prefix '" + prefix + "' of provider " + provider->id() + " rejected: already owned by " + it->second);
        }
    }
    providers_.push_back(provider);
    std::stable_sort(providers_.begin(), providers_.end(), [](const auto& a, const auto& b) {
        return a->priority() > b->priority();
    });
    NEXUS_LOG_DEBUG("Registered provider: " + provider->name() + " (" + provider->id() + ")");
}

void SearchEngine::unregister_provider(const std::string& provider_id) {
    std::lock_guard<std::mutex> lock(search_mutex_);
    auto it = std::remove_if(providers_.begin(), providers_.end(), [&](const auto& p) {
        return p->id() == provider_id;
    });
    providers_.erase(it, providers_.end());
    std::erase_if(prefix_owner_, [&](const auto& kv) { return kv.second == provider_id; });
}

void SearchEngine::set_provider_filter(std::function<bool(const std::string&)> is_enabled) {
    std::lock_guard<std::mutex> lock(search_mutex_);
    provider_filter_ = std::move(is_enabled);
}

std::vector<SearchResult> SearchEngine::search(const Query& query, size_t max_results) {
    std::atomic<bool> never_cancelled{false};
    return search(query, never_cancelled, max_results);
}

std::vector<SearchResult> SearchEngine::search(const Query& input, std::atomic<bool>& cancel_token, size_t max_results) {
    std::vector<SearchResult> combined_results;

    if (input.empty()) {
        return combined_results;
    }

    std::vector<SearchProviderPtr> current_providers;
    std::function<bool(const std::string&)> filter;
    std::string matched, owner;
    {
        std::lock_guard<std::mutex> lock(search_mutex_);
        current_providers = providers_;
        filter = provider_filter_;
        // Longest owned prefix wins; no match = unprefixed text for the general providers.
        const std::string trimmed = Query(input.raw()).text();
        for (const auto& [prefix, id] : prefix_owner_) {
            if (prefix.size() > matched.size() && trimmed.rfind(prefix, 0) == 0) {
                matched = prefix;
                owner = id;
            }
        }
    }
    const Query query(input.raw(), matched);
    // A prefixed query reaches only its owner; an unprefixed one only providers that claim "".
    current_providers.erase(std::remove_if(current_providers.begin(), current_providers.end(), [&](const auto& p) {
        if (!query.prefix().empty()) return p->id() != owner;
        const auto prefixes = p->prefixes();
        return std::find(prefixes.begin(), prefixes.end(), "") == prefixes.end();
    }), current_providers.end());
    if (filter) {
        current_providers.erase(std::remove_if(current_providers.begin(), current_providers.end(),
                                               [&](const auto& p) { return !filter(p->id()); }),
                                current_providers.end());
    }

    auto batch = std::make_shared<Batch>(query);
    batch->slots.resize(current_providers.size());
    batch->done.assign(current_providers.size(), 0);
    const auto deadline = std::chrono::steady_clock::now() + provider_budget_;
    std::vector<size_t> dispatched;
    for (size_t i = 0; i < current_providers.size(); ++i) {
        if (cancel_token.load()) break;
        const auto& provider = current_providers[i];
        std::shared_ptr<std::atomic<bool>> busy;
        {
            std::lock_guard<std::mutex> lock(search_mutex_);
            auto& flag = busy_[provider->id()];
            if (!flag) flag = std::make_shared<std::atomic<bool>>(false);
            busy = flag;
        }
        if (busy->exchange(true)) {
            NEXUS_LOG_DEBUG("Provider " + provider->id() + " still busy from an earlier search, skipped");
            continue;
        }
        dispatched.push_back(i);
        ++batch->pending;
        pool_->submit([batch, provider, busy, i] {
            std::optional<std::vector<SearchResult>> out;
            if (!batch->cancel.load()) {
                try {
                    out = provider->search(batch->query, batch->cancel);
                } catch (const std::exception& ex) {
                    NEXUS_LOG_ERROR(std::string("Provider ") + provider->id() + " threw exception: " + ex.what());
                } catch (...) {
                    NEXUS_LOG_ERROR(std::string("Provider ") + provider->id() + " threw unknown exception");
                }
            }
            busy->store(false);
            std::lock_guard<std::mutex> lock(batch->m);
            if (!batch->closed) {
                batch->slots[i] = std::move(out);
                batch->done[i] = 1;
            }
            --batch->pending;
            batch->cv.notify_all();
        });
    }

    {
        std::unique_lock<std::mutex> lock(batch->m);
        // Short slices so a cancel of the caller's token reaches the providers promptly. After a cancel we still
        // wait (up to the deadline) for the tasks to return, so their busy flags are clear for the next search.
        while (batch->pending > 0 && std::chrono::steady_clock::now() < deadline) {
            if (cancel_token.load()) batch->cancel.store(true);
            batch->cv.wait_until(lock, std::min(deadline, std::chrono::steady_clock::now() + std::chrono::milliseconds(5)));
        }
        batch->closed = true;
        if (batch->pending > 0) {
            batch->cancel.store(true);
            if (!cancel_token.load()) {
                for (size_t i : dispatched) {
                    if (!batch->done[i]) {
                        NEXUS_LOG_WARN("Provider " + current_providers[i]->id() + " exceeded its " +
                                       std::to_string(provider_budget_.count()) + " ms budget, results dropped");
                    }
                }
            }
        }
        if (cancel_token.load()) batch->cancel.store(true);
        for (auto& slot : batch->slots) {
            if (!slot) continue;
            combined_results.insert(combined_results.end(), std::make_move_iterator(slot->begin()),
                                    std::make_move_iterator(slot->end()));
        }
    }

    if (cancel_token.load()) {
        return {};
    }

    // Apply ranking
    ranking_engine_.rank(query, combined_results);

    // Limit to max_results
    if (combined_results.size() > max_results) {
        combined_results.resize(max_results);
    }

    return combined_results;
}

} // namespace nexus::core

