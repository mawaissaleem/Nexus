#pragma once

#include "nexus/core/search_provider.hpp"
#include "nexus/index/app_indexer.hpp"
#include "nexus/index/database.hpp"
#include <memory>

namespace nexus::providers {

class ApplicationProvider : public core::ISearchProvider {
public:
    ApplicationProvider(
        std::shared_ptr<index::AppIndexer> indexer,
        std::shared_ptr<index::Database> db = nullptr
    );

    [[nodiscard]] std::string id() const override { return "applications"; }
    [[nodiscard]] std::string name() const override { return "Applications"; }
    [[nodiscard]] std::string description() const override { return "Searches installed desktop applications"; }
    [[nodiscard]] int priority() const override { return 100; }

    std::vector<core::SearchResult> search(
        const core::Query& query,
        const std::atomic<bool>& cancel_token
    ) override;

private:
    std::shared_ptr<index::AppIndexer> indexer_;
    std::shared_ptr<index::Database> db_;
};

} // namespace nexus::providers

