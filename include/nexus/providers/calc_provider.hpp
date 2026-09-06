#pragma once

#include "nexus/core/search_provider.hpp"
#include <optional>

namespace nexus::providers {

class CalculatorProvider : public core::ISearchProvider {
public:
    [[nodiscard]] std::string id() const override { return "calculator"; }
    [[nodiscard]] std::string name() const override { return "Calculator"; }
    [[nodiscard]] std::string description() const override { return "Evaluates mathematical expressions"; }
    [[nodiscard]] int priority() const override { return 90; }

    std::vector<core::SearchResult> search(
        const core::Query& query,
        const std::atomic<bool>& cancel_token
    ) override;

    // Evaluate arithmetic expression safely without external interpreters
    static std::optional<double> evaluate(std::string_view expression);
};

} // namespace nexus::providers

