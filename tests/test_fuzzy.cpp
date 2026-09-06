#include "nexus/utils/fuzzy.hpp"
#include <iostream>
#include <cassert>

int main() {
    using nexus::utils::FuzzyMatcher;

    // Test 1: Exact match
    {
        auto res = FuzzyMatcher::match("firefox", "firefox");
        assert(res.matched);
        assert(res.score >= 1000.0);
    }

    // Test 2: Case insensitivity
    {
        auto res = FuzzyMatcher::match("Firefox", "firefox");
        assert(res.matched);
        assert(res.score >= 1000.0);
    }

    // Test 3: Prefix match
    {
        auto res = FuzzyMatcher::match("fire", "Firefox");
        assert(res.matched);
        assert(res.score >= 600.0);
    }

    // Test 4: Acronym match (vsc -> Visual Studio Code)
    {
        assert(FuzzyMatcher::is_acronym_match("vsc", "Visual Studio Code"));
        auto res = FuzzyMatcher::match("vsc", "Visual Studio Code");
        assert(res.matched);
        assert(res.score > 0.0);
    }

    // Test 5: Substring match
    {
        auto res = FuzzyMatcher::match("studio", "Visual Studio Code");
        assert(res.matched);
        assert(res.score >= 400.0);
    }

    // Test 6: Fuzzy match
    {
        auto res = FuzzyMatcher::match("vscd", "Visual Studio Code");
        assert(res.matched);
    }

    // Test 7: Non-match
    {
        auto res = FuzzyMatcher::match("xyz123", "Visual Studio Code");
        assert(!res.matched);
    }

    // Test 8: Empty pattern
    {
        auto res = FuzzyMatcher::match("", "Any target");
        assert(res.matched);
    }

    std::cout << "All fuzzy matching tests passed successfully!\n";
    return 0;
}

