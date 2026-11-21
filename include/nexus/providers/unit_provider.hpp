#pragma once

#include "nexus/core/search_provider.hpp"

namespace nexus::providers {

class UnitProvider : public core::ISearchProvider {
public:
    [[nodiscard]] std::string id() const override { return "units"; }
    [[nodiscard]] std::string name() const override { return "Unit converter"; }
    [[nodiscard]] std::string description() const override {
        return "Converts lengths, weights, temperatures and more";
    }
    [[nodiscard]] std::vector<std::string> prefixes() const override { return {""}; }
    [[nodiscard]] int priority() const override { return 92; }

    std::vector<core::SearchResult> search(const core::Query& query, const std::atomic<bool>& cancel_token) override;
};

}  // namespace nexus::providers
