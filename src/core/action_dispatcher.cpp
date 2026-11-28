#include "nexus/core/action_dispatcher.hpp"
#include "nexus/core/executor.hpp"
#include <optional>

namespace nexus::core {

utils::Status dispatch_action(const SearchResult& result, const std::vector<SearchProviderPtr>& providers,
                              ShellFailureFn on_shell_failure) {
    // Only an Internal result is the hook itself; for the other kinds the hook's outcome doesn't stop the action.
    std::optional<utils::Status> hook;
    for (const auto& p : providers) {
        if (p && p->id() == result.provider_id) {
            hook = p->on_executed(result);
            break;
        }
    }

    switch (result.action_kind) {
        case ActionKind::Launch:
            return Executor::launch_desktop_exec(result.execution_payload, result.terminal);
        case ActionKind::Open:
            return Executor::open_path_or_url(result.execution_payload);
        case ActionKind::Shell:
            return Executor::launch_shell_command(result.execution_payload, std::move(on_shell_failure));
        case ActionKind::Copy:
            return {};
        case ActionKind::Internal:
            if (hook.has_value()) return *hook;
            return utils::Status::error("can't run \"" + result.title + "\": nothing handles this action");
        case ActionKind::None:
            break;
    }
    return utils::Status::error("can't run \"" + result.title + "\": it has no action");
}

}  // namespace nexus::core
