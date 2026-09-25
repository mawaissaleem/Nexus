#include "nexus/core/search_engine.hpp"
#include "nexus/utils/logger.hpp"
#include <algorithm>

namespace nexus::core {

SearchEngine::SearchEngine(RankingEngine ranking_engine)
    : ranking_engine_(std::move(ranking_engine)) {}

SearchEngine::~SearchEngine() {
    cancel();
}

void SearchEngine::register_provider(SearchProviderPtr provider) {
    if (!provider) return;
    std::lock_guard<std::mutex> lock(search_mutex_);
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
}

std::vector<SearchResult> SearchEngine::search(const Query& query, size_t max_results) {
    return search(query, current_cancel_token_, max_results);
}

std::vector<SearchResult> SearchEngine::search(const Query& query, std::atomic<bool>& cancel_token, size_t max_results) {
    std::vector<SearchResult> combined_results;

    if (query.empty()) {
        return combined_results;
    }

    std::vector<SearchProviderPtr> current_providers;
    {
        std::lock_guard<std::mutex> lock(search_mutex_);
        current_providers = providers_;
    }

    for (const auto& provider : current_providers) {
        if (cancel_token.load()) {
            break;
        }

        try {
            auto provider_results = provider->search(query, cancel_token);
            combined_results.insert(
                combined_results.end(),
                std::make_move_iterator(provider_results.begin()),
                std::make_move_iterator(provider_results.end())
            );
        } catch (const std::exception& ex) {
            NEXUS_LOG_ERROR(std::string("Provider ") + provider->id() + " threw exception: " + ex.what());
        } catch (...) {
            NEXUS_LOG_ERROR(std::string("Provider ") + provider->id() + " threw unknown exception");
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

std::future<std::vector<SearchResult>> SearchEngine::search_async(const Query& query, size_t max_results) {
    cancel();
    current_cancel_token_.store(false);

    return std::async(std::launch::async, [this, query, max_results]() {
        return this->search(query, max_results);
    });
}

void SearchEngine::cancel() {
    current_cancel_token_.store(true);
}

} // namespace nexus::core

