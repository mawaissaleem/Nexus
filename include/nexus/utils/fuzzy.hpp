#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <cstddef>

namespace nexus::utils {

struct FuzzyMatchResult {
    bool matched{false};
    double score{0.0};
    std::vector<size_t> matched_indices;
};

class FuzzyMatcher {
public:
    // Match pattern against target string.
    // Returns match status, calculated score, and character positions matched.
    static FuzzyMatchResult match(std::string_view pattern, std::string_view target);

    // Helper: checks if target contains query as acronym (e.g., "vsc" in "Visual Studio Code")
    static bool is_acronym_match(std::string_view pattern, std::string_view target);

    // Case-insensitive string equality
    static bool iequals(std::string_view a, std::string_view b);

    // Case-insensitive starts_with
    static bool istarts_with(std::string_view target, std::string_view prefix);

    // Case-insensitive substring search
    static bool icontains(std::string_view target, std::string_view sub);

    // Lowercase conversion
    static std::string to_lower(std::string_view s);
};

} // namespace nexus::utils

