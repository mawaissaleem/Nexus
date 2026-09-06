#pragma once

#include "nexus/core/search_provider.hpp"
#include <vector>
#include <string>

namespace nexus::providers {

class FileProvider : public core::ISearchProvider {
public:
    explicit FileProvider(size_t max_depth = 3);

    [[nodiscard]] std::string id() const override { return "files"; }
    [[nodiscard]] std::string name() const override { return "Files & Directories"; }
    [[nodiscard]] std::string description() const override { return "Searches files and folders in configured paths"; }
    [[nodiscard]] int priority() const override { return 70; }

    std::vector<core::SearchResult> search(
        const core::Query& query,
        const std::atomic<bool>& cancel_token
    ) override;

    void set_max_depth(size_t depth) { max_depth_ = depth; }

private:
    size_t max_depth_{3};
};

} // namespace nexus::providers
