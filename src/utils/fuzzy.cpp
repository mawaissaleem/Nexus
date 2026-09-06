#include "nexus/utils/fuzzy.hpp"
#include <algorithm>
#include <cctype>

namespace nexus::utils {

std::string FuzzyMatcher::to_lower(std::string_view s) {
    std::string result;
    result.reserve(s.size());
    for (char c : s) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return result;
}

bool FuzzyMatcher::iequals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) {
        return false;
    }
    return std::equal(a.begin(), a.end(), b.begin(), [](char c1, char c2) {
        return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
    });
}

bool FuzzyMatcher::istarts_with(std::string_view target, std::string_view prefix) {
    if (prefix.size() > target.size()) {
        return false;
    }
    return std::equal(prefix.begin(), prefix.end(), target.begin(), [](char c1, char c2) {
        return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
    });
}

bool FuzzyMatcher::icontains(std::string_view target, std::string_view sub) {
    if (sub.empty()) return true;
    if (sub.size() > target.size()) return false;

    auto it = std::search(
        target.begin(), target.end(),
        sub.begin(), sub.end(),
        [](char c1, char c2) {
            return std::tolower(static_cast<unsigned char>(c1)) == std::tolower(static_cast<unsigned char>(c2));
        }
    );
    return it != target.end();
}

bool FuzzyMatcher::is_acronym_match(std::string_view pattern, std::string_view target) {
    if (pattern.empty() || target.empty()) return false;

    std::string acronym;
    bool in_word = false;

    for (size_t i = 0; i < target.size(); ++i) {
        char c = target[i];
        bool is_word_char = std::isalnum(static_cast<unsigned char>(c));

        // Start of word: alphanumeric following non-alnum, or uppercase camelCase
        bool is_boundary = false;
        if (is_word_char) {
            if (!in_word) {
                is_boundary = true;
            } else if (std::isupper(static_cast<unsigned char>(c)) &&
                       i > 0 && std::islower(static_cast<unsigned char>(target[i - 1]))) {
                is_boundary = true;
            }
        }

        if (is_boundary) {
            acronym.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
        }

        in_word = is_word_char;
    }

    std::string pat_lower = to_lower(pattern);
    return acronym.find(pat_lower) != std::string::npos;
}

FuzzyMatchResult FuzzyMatcher::match(std::string_view pattern, std::string_view target) {
    FuzzyMatchResult res;
    if (pattern.empty()) {
        res.matched = true;
        res.score = 0.0;
        return res;
    }

    if (target.empty() || pattern.size() > target.size()) {
        res.matched = false;
        return res;
    }

    // 1. Exact Match
    if (iequals(pattern, target)) {
        res.matched = true;
        res.score = 1000.0;
        res.matched_indices.resize(pattern.size());
        for (size_t i = 0; i < pattern.size(); ++i) {
            res.matched_indices[i] = i;
        }
        return res;
    }

    // 2. Prefix Match
    if (istarts_with(target, pattern)) {
        res.matched = true;
        double ratio = static_cast<double>(pattern.size()) / static_cast<double>(target.size());
        res.score = 600.0 + (150.0 * ratio);
        res.matched_indices.resize(pattern.size());
        for (size_t i = 0; i < pattern.size(); ++i) {
            res.matched_indices[i] = i;
        }
        return res;
    }

    // 3. Acronym Match (e.g., 'vsc' -> 'Visual Studio Code')
    bool acronym_hit = is_acronym_match(pattern, target);

    // 4. Substring Match check
    std::string target_lower = to_lower(target);
    std::string pattern_lower = to_lower(pattern);
    size_t substr_pos = target_lower.find(pattern_lower);
    if (substr_pos != std::string::npos) {
        res.matched = true;
        double score = 400.0;
        // Word boundary bonus for substring
        if (substr_pos == 0 || !std::isalnum(static_cast<unsigned char>(target[substr_pos - 1]))) {
            score += 100.0;
        }
        double ratio = static_cast<double>(pattern.size()) / static_cast<double>(target.size());
        res.score = score + (50.0 * ratio);
        res.matched_indices.resize(pattern.size());
        for (size_t i = 0; i < pattern.size(); ++i) {
            res.matched_indices[i] = substr_pos + i;
        }
        if (acronym_hit) res.score += 50.0;
        return res;
    }

    // 5. Fuzzy character scan
    size_t pattern_idx = 0;
    size_t last_matched_target_idx = 0;
    bool prev_matched = false;
    double score = 0.0;

    for (size_t target_idx = 0; target_idx < target.size() && pattern_idx < pattern.size(); ++target_idx) {
        char p_char = pattern_lower[pattern_idx];
        char t_char = target_lower[target_idx];

        if (p_char == t_char) {
            res.matched_indices.push_back(target_idx);

            // Base point for char match
            score += 10.0;

            // Check word boundary
            bool is_boundary = (target_idx == 0) ||
                               !std::isalnum(static_cast<unsigned char>(target[target_idx - 1])) ||
                               (std::isupper(static_cast<unsigned char>(target[target_idx])) &&
                                std::islower(static_cast<unsigned char>(target[target_idx - 1])));
            if (is_boundary) {
                score += 35.0;
            }

            // Consecutive match bonus
            if (prev_matched && (target_idx == last_matched_target_idx + 1)) {
                score += 25.0;
            } else if (pattern_idx > 0) {
                // Gap penalty
                size_t gap = target_idx - last_matched_target_idx - 1;
                score -= std::min(static_cast<double>(gap) * 1.5, 20.0);
            }

            prev_matched = true;
            last_matched_target_idx = target_idx;
            ++pattern_idx;
        } else {
            prev_matched = false;
        }
    }

    if (pattern_idx == pattern.size()) {
        res.matched = true;
        if (acronym_hit) {
            score += 100.0;
        }
        // Normalize length ratio penalty
        double length_ratio = static_cast<double>(pattern.size()) / static_cast<double>(target.size());
        res.score = std::max(1.0, score + (50.0 * length_ratio));
        return res;
    }

    // If not all matched but acronym matches
    if (acronym_hit) {
        res.matched = true;
        res.score = 150.0;
        return res;
    }

    res.matched = false;
    res.score = 0.0;
    res.matched_indices.clear();
    return res;
}

} // namespace nexus::utils

