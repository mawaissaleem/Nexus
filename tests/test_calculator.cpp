#include "nexus/providers/calc_provider.hpp"
#include <iostream>
#include <cassert>
#include <clocale>
#include <cmath>
#include <cstring>
#include <string>

namespace {

// The GUI runs with the user's LC_NUMERIC, so try a few comma-decimal locales; CI may have none installed
bool use_comma_locale() {
    for (const char* name : {"de_DE.UTF-8", "fr_FR.UTF-8", "en_DK.utf8"}) {
        const bool switched = std::setlocale(LC_NUMERIC, name) != nullptr;
        if (switched && std::strcmp(std::localeconv()->decimal_point, ",") == 0) {
            return true;
        }
    }
    return false;
}

}  // namespace

int main() {
    using nexus::providers::CalculatorProvider;

    // Test 1: Simple multiplication
    {
        auto val = CalculatorProvider::evaluate("25 * 17");
        assert(val.has_value());
        assert(*val == 425.0);
    }

    // Test 2: Operator precedence
    {
        auto val = CalculatorProvider::evaluate("2 + 3 * 4");
        assert(val.has_value());
        assert(*val == 14.0);
    }

    // Test 3: Parentheses
    {
        auto val = CalculatorProvider::evaluate("(2 + 3) * 4");
        assert(val.has_value());
        assert(*val == 20.0);
    }

    // Test 4: Functions (sqrt)
    {
        auto val = CalculatorProvider::evaluate("sqrt(144)");
        assert(val.has_value());
        assert(*val == 12.0);
    }

    // Test 5: Power
    {
        auto val = CalculatorProvider::evaluate("2 ^ 3");
        assert(val.has_value());
        assert(*val == 8.0);
    }

    // Test 6: Decimals
    {
        auto val = CalculatorProvider::evaluate("10.5 + 4.5");
        assert(val.has_value());
        assert(*val == 15.0);
    }

    // Test 7: Invalid expression
    {
        auto val = CalculatorProvider::evaluate("2 +* 3");
        assert(!val.has_value());
    }

    // Test 8: A comma locale changes neither parsing nor the shown result
    {
        const std::string previous = std::setlocale(LC_NUMERIC, nullptr);
        if (use_comma_locale()) {
            auto val = CalculatorProvider::evaluate("1.5+1");
            assert(val.has_value());
            assert(*val == 2.5);

            CalculatorProvider provider;
            const std::atomic<bool> no_cancel{false};
            const auto hits = provider.search(nexus::core::Query("1.5+1"), no_cancel);
            assert(hits.size() == 1);
            assert(hits[0].title == "2.5");
        } else {
            std::cout << "No comma-decimal locale installed, skipping the locale check\n";
        }
        std::setlocale(LC_NUMERIC, previous.c_str());
    }

    std::cout << "All CalculatorProvider tests passed successfully!\n";
    return 0;
}
