#pragma once

#include "nexus/core/alias_manager.hpp"
#include "nexus/core/search_provider.hpp"
#include <string>

namespace nexus::providers {

class AliasProvider : public core::ISearchProvider {
public:
    explicit AliasProvider(core::AliasManager& aliases) : aliases_(aliases) {}

    [[nodiscard]] std::string id() const override { return "alias"; }
    [[nodiscard]] std::string name() const override { return "Custom Aliases"; }
    [[nodiscard]] std::string description() const override { return "User-defined nicknames for links, directories, and commands"; }
    [[nodiscard]] int priority() const override { return 120; }

    std::vector<core::SearchResult> search(
        const core::Query& query,
        const std::atomic<bool>& cancel_token
    ) override;

private:
    core::AliasManager& aliases_;
};

} // namespace nexus::providers
