#pragma once

#include "nexus/core/search_provider.hpp"
#include "nexus/index/database.hpp"
#include <memory>
#include <vector>
#include <string>

namespace nexus::providers {

class FileProvider : public core::ISearchProvider {
public:
    // Reads the file index only (built by index::FileIndexer); never touches the filesystem.
    explicit FileProvider(std::shared_ptr<index::Database> db);

    [[nodiscard]] std::string id() const override { return "files"; }
    [[nodiscard]] std::string name() const override { return "Files & Directories"; }
    [[nodiscard]] std::string description() const override { return "Searches files and folders in configured paths"; }
    [[nodiscard]] std::vector<std::string> prefixes() const override { return {"file "}; }
    [[nodiscard]] int priority() const override { return 70; }

    std::vector<core::SearchResult> search(
        const core::Query& query,
        const std::atomic<bool>& cancel_token
    ) override;

private:
    std::shared_ptr<index::Database> db_;
};

} // namespace nexus::providers
