#pragma once

#include "nexus/core/result.hpp"
#include "nexus/core/search_provider.hpp"
#include <vector>

namespace nexus::core {

// Executes a result's action: runs the provider's on_executed hook, then maps the ActionKind to an Executor call.
// Internal results are executed by the hook of the provider named in `provider_id` (false if none is registered).
// Returns true on success.
bool dispatch_action(const SearchResult& result, const std::vector<SearchProviderPtr>& providers);

} // namespace nexus::core
