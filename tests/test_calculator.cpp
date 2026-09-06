#include "nexus/providers/calc_provider.hpp"
#include <iostream>
#include <cassert>
#include <cmath>

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

    std::cout << "All CalculatorProvider tests passed successfully!\n";
    return 0;
}

