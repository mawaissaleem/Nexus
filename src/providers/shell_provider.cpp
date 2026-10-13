#include "nexus/providers/shell_provider.hpp"

namespace nexus::providers {

std::vector<core::SearchResult> ShellProvider::search(
    const core::Query& query,
    const std::atomic<bool>& cancel_token
) {
    if (cancel_token.load() || query.prefix() != ">") {
        return {};
    }

    const std::string& cmd = query.text();
    if (cmd.empty()) {
        return {};
    }

    core::SearchResult res;
    res.id = "shell:" + cmd;
    res.title = "> " + cmd;
    res.subtitle = "Execute command";
    res.icon = "utilities-terminal";
    res.provider_id = id();
    res.score = 1000.0;
    res.execution_payload = cmd;
    res.action_kind = core::ActionKind::Shell;

    return {res};
}

} // namespace nexus::providers

