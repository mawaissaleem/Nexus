#pragma once

#include "nexus/core/executor.hpp"
#include "nexus/core/result.hpp"
#include "nexus/core/search_provider.hpp"
#include <vector>

namespace nexus::core {

// Executes a result's action: runs the provider's on_executed hook, then maps the ActionKind to an Executor call.
// Internal results are executed by the hook of the provider named in `provider_id` (an error if none is registered).
// The returned Status says why when the action failed.
// `on_shell_failure` (optional) hears about a Shell command that later fails; see Executor.
utils::Status dispatch_action(const SearchResult& result, const std::vector<SearchProviderPtr>& providers,
                              ShellFailureFn on_shell_failure = {});

}  // namespace nexus::core
