#pragma once

#include "nexus/core/search_provider.hpp"

namespace nexus::providers {

class ShellProvider : public core::ISearchProvider {
public:
    [[nodiscard]] std::string id() const override { return "shell"; }
    [[nodiscard]] std::string name() const override { return "Terminal Command"; }
    [[nodiscard]] std::string description() const override { return "Executes shell commands"; }
    [[nodiscard]] int priority() const override { return 80; }

    std::vector<core::SearchResult> search(
        const core::Query& query,
        const std::atomic<bool>& cancel_token
    ) override;
};

} // namespace nexus::providers

