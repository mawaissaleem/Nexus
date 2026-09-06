#pragma once

#include "nexus/core/query.hpp"
#include "nexus/core/result.hpp"
#include <string>
#include <vector>
#include <atomic>
#include <memory>

namespace nexus::core {

class ISearchProvider {
public:
    virtual ~ISearchProvider() = default;

    [[nodiscard]] virtual std::string id() const = 0;
    [[nodiscard]] virtual std::string name() const = 0;
    [[nodiscard]] virtual std::string description() const { return ""; }
    [[nodiscard]] virtual int priority() const { return 0; }

    // Execute search for query. If cancel_token becomes true, provider should abort early.
    virtual std::vector<SearchResult> search(
        const Query& query,
        const std::atomic<bool>& cancel_token
    ) = 0;
};

using SearchProviderPtr = std::shared_ptr<ISearchProvider>;

} // namespace nexus::core

