#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstddef>
#include <cstdint>

namespace nexus::core {

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

    // Executable action callback. Returns true on success.
    std::function<bool()> action;
    std::string execution_payload; // E.g. shell command, path, or URL
};

} // namespace nexus::core

