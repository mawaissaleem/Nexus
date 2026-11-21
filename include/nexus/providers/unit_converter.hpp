#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace nexus::providers {

struct UnitConversion {
    double input{0.0};
    double result{0.0};
    std::string from;  // unit exactly as typed
    std::string to;
};

// Converts "<number> <unit> in|to <unit>" (space after the number optional, a leading + or - allowed).
// Returns nullopt for anything else: unknown unit, units of different kinds, or a word like "log in".
// Units are case-insensitive, except that b is a bit, B a byte, and Kb/Mb/Gb/Tb are bits while
// KB/MB/GB/TB (any other casing) are bytes.
[[nodiscard]] std::optional<UnitConversion> convert_units(std::string_view input);

// Six significant digits, plain digits (no exponent), no trailing zeros and always a '.' whatever the
// locale: 1000000, 74.5645, 0.0123457, 0.000000000000125.
[[nodiscard]] std::string format_unit_value(double value);

}  // namespace nexus::providers
