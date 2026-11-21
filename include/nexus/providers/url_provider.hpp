#pragma once

#include "nexus/core/search_provider.hpp"
#include <optional>
#include <string>
#include <string_view>

namespace nexus::providers {

class UrlProvider : public core::ISearchProvider {
public:
    [[nodiscard]] std::string id() const override { return "url"; }
    [[nodiscard]] std::string name() const override { return "URL"; }
    [[nodiscard]] std::string description() const override { return "Opens web addresses in the browser"; }
    [[nodiscard]] std::vector<std::string> prefixes() const override { return {""}; }
    [[nodiscard]] int priority() const override { return 95; }

    std::vector<core::SearchResult> search(const core::Query& query, const std::atomic<bool>& cancel_token) override;

    // The http(s) URL to open for `input`, or nullopt when the whole input is not a web address.
    // shortcut: bare file names are caught by a short extension list, so x.md or x.py (real TLDs) still pass;
    // upgrade to a TLD list if that noise matters.
    static std::optional<std::string> normalize(std::string_view input);
};

}  // namespace nexus::providers
