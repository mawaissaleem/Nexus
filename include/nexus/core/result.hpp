#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <cstddef>
#include <cstdint>

namespace nexus::core {

// What executing a result does. Plain data, so a result can be stored, replayed or sent across a process boundary.
enum class ActionKind {
    None,      // nothing to execute
    Launch,    // desktop Exec line in `execution_payload` (no shell); `terminal` runs it in a terminal
    Open,      // file or URL via the default handler
    Shell,     // run through the shell: only ShellProvider and `>` aliases may produce this (D-71)
    Copy,      // text for the clipboard; the clipboard lives in the UI layer, so core does nothing
    Internal,  // handled by the provider with `provider_id` (ISearchProvider::on_executed)
};

struct SearchResult {
    std::string id;
    std::string title;
    std::string subtitle;
    std::string icon;
    std::string provider_id;
    double score{0.0};
    std::unordered_map<std::string, std::string> metadata;
    // Ranking signals: launches from a similar query and the newest one (Unix s); 0 = none.
    int usage_count{0};
    std::int64_t last_used{0};
    std::vector<size_t> matched_indices; // Character indices in title that matched

    ActionKind action_kind{ActionKind::None};
    std::string execution_payload; // Exec line, shell command, path, URL or text; meaning set by action_kind
    bool terminal{false};          // Launch only
    std::string query;             // raw query that produced this result (usage history)
};

} // namespace nexus::core

