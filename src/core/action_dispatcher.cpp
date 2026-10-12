#include "nexus/core/action_dispatcher.hpp"
#include "nexus/core/executor.hpp"

namespace nexus::core {

bool dispatch_action(const SearchResult& result, const std::vector<SearchProviderPtr>& providers) {
    bool hook_ok = result.action_kind != ActionKind::Internal;
    for (const auto& p : providers) {
        if (p && p->id() == result.provider_id) {
            const bool ok = p->on_executed(result);
            if (result.action_kind == ActionKind::Internal) hook_ok = ok;
            break;
        }
    }

    switch (result.action_kind) {
        case ActionKind::Launch: return Executor::launch_desktop_exec(result.execution_payload, result.terminal);
        case ActionKind::Open: return Executor::open_path_or_url(result.execution_payload);
        case ActionKind::Shell:
            return !result.execution_payload.empty() && Executor::launch_shell_command(result.execution_payload);
        case ActionKind::Copy: return true;
        case ActionKind::Internal: return hook_ok;
        case ActionKind::None: break;
    }
    return false;
}

} // namespace nexus::core
