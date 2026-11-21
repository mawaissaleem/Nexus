#include "nexus/providers/unit_converter.hpp"
#include "nexus/utils/fuzzy.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace nexus::providers {

namespace {

enum class Kind { Length, Mass, Temperature, Data, Time, Volume, Speed };

// base = value * factor + offset; the base unit is per kind (metre, gram, kelvin, bit, second, litre, m/s)
struct Unit {
    Kind kind;
    double factor;
    double offset;
};

struct UnitDef {
    std::vector<std::string_view> names;
    Unit unit;
};

constexpr double kBitsPerByte = 8.0;

// Names are lowercase; the few case-sensitive ones (b, B, Kb, Mb, Gb, Tb) are listed in their exact spelling.
const std::vector<UnitDef>& unit_defs() {
    static const std::vector<UnitDef> defs = {
        {{"mm", "millimeter", "millimeters", "millimetre", "millimetres"}, {Kind::Length, 0.001, 0}},
        {{"cm", "centimeter", "centimeters", "centimetre", "centimetres"}, {Kind::Length, 0.01, 0}},
        {{"m", "meter", "meters", "metre", "metres"}, {Kind::Length, 1, 0}},
        {{"km", "kilometer", "kilometers", "kilometre", "kilometres"}, {Kind::Length, 1000, 0}},
        {{"in", "inch", "inches"}, {Kind::Length, 0.0254, 0}},
        {{"ft", "foot", "feet"}, {Kind::Length, 0.3048, 0}},
        {{"yd", "yard", "yards"}, {Kind::Length, 0.9144, 0}},
        {{"mi", "mile", "miles"}, {Kind::Length, 1609.344, 0}},

        {{"mg", "milligram", "milligrams"}, {Kind::Mass, 0.001, 0}},
        {{"g", "gram", "grams"}, {Kind::Mass, 1, 0}},
        {{"kg", "kilogram", "kilograms"}, {Kind::Mass, 1000, 0}},
        {{"t", "tonne", "tonnes"}, {Kind::Mass, 1000000, 0}},
        {{"oz", "ounce", "ounces"}, {Kind::Mass, 28.349523125, 0}},
        {{"lb", "lbs", "pound", "pounds"}, {Kind::Mass, 453.59237, 0}},

        {{"c", "°c", "celsius"}, {Kind::Temperature, 1, 273.15}},
        {{"f", "°f", "fahrenheit"}, {Kind::Temperature, 5.0 / 9.0, 273.15 - 32.0 * 5.0 / 9.0}},
        {{"k", "kelvin"}, {Kind::Temperature, 1, 0}},

        {{"b", "bit", "bits"}, {Kind::Data, 1, 0}},
        {{"B", "byte", "bytes"}, {Kind::Data, kBitsPerByte, 0}},
        {{"Kb"}, {Kind::Data, 1e3, 0}},
        {{"Mb"}, {Kind::Data, 1e6, 0}},
        {{"Gb"}, {Kind::Data, 1e9, 0}},
        {{"Tb"}, {Kind::Data, 1e12, 0}},
        {{"kb", "kilobyte", "kilobytes"}, {Kind::Data, 1e3 * kBitsPerByte, 0}},
        {{"mb", "megabyte", "megabytes"}, {Kind::Data, 1e6 * kBitsPerByte, 0}},
        {{"gb", "gigabyte", "gigabytes"}, {Kind::Data, 1e9 * kBitsPerByte, 0}},
        {{"tb", "terabyte", "terabytes"}, {Kind::Data, 1e12 * kBitsPerByte, 0}},
        {{"kib", "kibibyte", "kibibytes"}, {Kind::Data, 1024.0 * kBitsPerByte, 0}},
        {{"mib", "mebibyte", "mebibytes"}, {Kind::Data, 1048576.0 * kBitsPerByte, 0}},
        {{"gib", "gibibyte", "gibibytes"}, {Kind::Data, 1073741824.0 * kBitsPerByte, 0}},
        {{"tib", "tebibyte", "tebibytes"}, {Kind::Data, 1099511627776.0 * kBitsPerByte, 0}},

        {{"ms", "millisecond", "milliseconds"}, {Kind::Time, 0.001, 0}},
        {{"s", "sec", "second", "seconds"}, {Kind::Time, 1, 0}},
        {{"min", "minute", "minutes"}, {Kind::Time, 60, 0}},
        {{"h", "hr", "hour", "hours"}, {Kind::Time, 3600, 0}},
        {{"day", "days"}, {Kind::Time, 86400, 0}},
        {{"week", "weeks"}, {Kind::Time, 604800, 0}},

        {{"ml", "milliliter", "milliliters", "millilitre", "millilitres"}, {Kind::Volume, 0.001, 0}},
        {{"l", "liter", "liters", "litre", "litres"}, {Kind::Volume, 1, 0}},
        {{"gal", "gallon", "gallons"}, {Kind::Volume, 3.785411784, 0}},

        {{"km/h", "kmh", "kph"}, {Kind::Speed, 1000.0 / 3600.0, 0}},
        {{"mph"}, {Kind::Speed, 1609.344 / 3600.0, 0}},
        {{"m/s"}, {Kind::Speed, 1, 0}},
    };
    return defs;
}

const std::unordered_map<std::string_view, Unit>& unit_table() {
    static const std::unordered_map<std::string_view, Unit> table = [] {
        std::unordered_map<std::string_view, Unit> built;
        for (const auto& def : unit_defs()) {
            for (const auto name : def.names) {
                built.emplace(name, def.unit);
            }
        }
        return built;
    }();
    return table;
}

// Exact spelling first so Mb and MB can differ, then lowercase for everything else
std::optional<Unit> find_unit(std::string_view name) {
    const auto& table = unit_table();
    if (const auto exact = table.find(name); exact != table.end()) {
        return exact->second;
    }
    const std::string lower = utils::FuzzyMatcher::to_lower(name);
    if (const auto folded = table.find(lower); folded != table.end()) {
        return folded->second;
    }
    return std::nullopt;
}

bool is_number_char(char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0 || c == '.'; }

struct SplitInput {
    double number;
    std::string_view from;
    std::string_view to;
};

// "<number> <unit> in|to <unit>": one word per unit, the separator is the first in/to after the first word
std::optional<SplitInput> split_input(std::string_view input) {
    std::size_t number_start = 0;
    std::size_t pos = 0;
    // from_chars takes a leading minus but not a plus, so a plus is skipped here
    if (input.starts_with('+')) {
        number_start = 1;
        pos = 1;
    } else if (input.starts_with('-')) {
        pos = 1;
    }
    const std::size_t digits_start = pos;
    while (pos < input.size() && is_number_char(input[pos])) {
        ++pos;
    }

    double number = 0.0;
    const auto parsed = std::from_chars(input.data() + number_start, input.data() + pos, number);
    if (pos == digits_start || parsed.ec != std::errc{} || parsed.ptr != input.data() + pos) {
        return std::nullopt;
    }

    std::vector<std::string_view> words;
    std::string_view rest = input.substr(pos);
    while (!rest.empty()) {
        rest.remove_prefix(std::min(rest.find_first_not_of(' '), rest.size()));
        const std::size_t end = std::min(rest.find(' '), rest.size());
        if (end > 0) {
            words.push_back(rest.substr(0, end));
        }
        rest.remove_prefix(end);
    }

    if (words.size() != 3) {
        return std::nullopt;
    }
    const std::string separator = utils::FuzzyMatcher::to_lower(words[1]);
    if (separator != "in" && separator != "to") {
        return std::nullopt;
    }
    return SplitInput{number, words[0], words[2]};
}

}  // namespace

std::optional<UnitConversion> convert_units(std::string_view input) {
    const auto split = split_input(input);
    if (!split) {
        return std::nullopt;
    }

    const auto from = find_unit(split->from);
    const auto to = find_unit(split->to);
    if (!from || !to || from->kind != to->kind) {
        return std::nullopt;
    }

    const double scaled = split->number * from->factor;
    const double base = scaled + from->offset;
    double result = (base - to->offset) / to->factor;
    if (!std::isfinite(result)) {
        return std::nullopt;
    }

    // Offsets leave rounding noise where the answer is zero (-459.67 f to k gave -2.8e-14)
    constexpr double kRoundingNoise = 1e-12;
    const double largest_term = std::max({std::fabs(scaled), std::fabs(from->offset), std::fabs(to->offset)});
    if (std::fabs(result) < kRoundingNoise * largest_term / to->factor) {
        result = 0.0;
    }
    return UnitConversion{split->number, result, std::string(split->from), std::string(split->to)};
}

std::string format_unit_value(double value) {
    if (value == 0.0 || !std::isfinite(value)) {
        return "0";
    }

    // Six significant digits however small the value, so a tiny result never shows as a false 0
    constexpr int kSignificantDigits = 6;
    const int integer_digits = static_cast<int>(std::floor(std::log10(std::fabs(value)))) + 1;
    const int decimals = std::max(kSignificantDigits - integer_digits, 0);

    // Fits the extremes: the largest double has 309 integer digits, the smallest needs about 330 decimals
    std::array<char, 400> buffer{};
    // to_chars ignores the locale, unlike printf, which writes 1,5 under a comma locale
    const auto written =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value, std::chars_format::fixed, decimals);
    if (written.ec != std::errc{}) {
        return "0";
    }
    std::string text(buffer.data(), written.ptr);

    if (text.find('.') != std::string::npos) {
        text.erase(text.find_last_not_of('0') + 1);
        if (text.back() == '.') {
            text.pop_back();
        }
    }
    return text;
}

}  // namespace nexus::providers
