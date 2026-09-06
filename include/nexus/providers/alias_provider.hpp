#pragma once

#include "nexus/core/search_provider.hpp"
#include <string>

namespace nexus::providers {

class AliasProvider : public core::ISearchProvider {
public:
    [[nodiscard]] std::string id() const override { return "alias"; }
    [[nodiscard]] std::string name() const override { return "Custom Aliases"; }
    [[nodiscard]] std::string description() const override { return "User-defined nicknames for links, directories, and commands"; }
    [[nodiscard]] int priority() const override { return 120; }

    std::vector<core::SearchResult> search(
        const core::Query& query,
        const std::atomic<bool>& cancel_token
    ) override;
};

} // namespace nexus::providers
