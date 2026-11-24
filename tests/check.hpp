#pragma once

#include <cstdlib>
#include <iostream>

// assert(a == b) only says "a == b failed"; this also prints both values.
// Aborts like assert(), so it needs no -UNDEBUG and a failing suite still fails ctest.
#define CHECK_EQ(actual, expected)                                                                                     \
    do {                                                                                                               \
        const auto& nexus_actual = (actual);                                                                           \
        const auto& nexus_expected = (expected);                                                                       \
        if (!(nexus_actual == nexus_expected)) {                                                                       \
            std::cerr << __FILE__ << ":" << __LINE__ << ": CHECK_EQ(" #actual ", " #expected                           \
                      << ") failed\n  actual:   " << nexus_actual << "\n  expected: " << nexus_expected << std::endl;  \
            std::abort();                                                                                              \
        }                                                                                                              \
    } while (false)
