#include "nexus/core/search_engine.hpp"
#include "nexus/core/search_provider.hpp"
#include "nexus/providers/calc_provider.hpp"
#include "nexus/providers/unit_converter.hpp"
#include "nexus/providers/unit_provider.hpp"
#include <cassert>
#include <clocale>
#include <cmath>
#include <cstring>
#include <ctime>
#include <iostream>
#include <memory>
#include <string>

using nexus::core::ActionKind;
using nexus::core::Query;
using nexus::core::SearchResult;
using nexus::providers::convert_units;
using nexus::providers::format_unit_value;
using nexus::providers::UnitProvider;

namespace {

// The strongest app hit a conversion can meet: top app score, launched often and just now.
class HeavyAppProvider : public nexus::core::ISearchProvider {
public:
    [[nodiscard]] std::string id() const override { return "applications"; }
    [[nodiscard]] std::string name() const override { return "Applications"; }

    std::vector<SearchResult> search(const Query& /*query*/, const std::atomic<bool>& /*cancel_token*/) override {
        SearchResult app;
        app.id = "app:km";
        app.title = "Kilometer Tracker";
        app.provider_id = id();
        app.score = 1000.0;
        app.usage_count = 10000;
        app.last_used = std::time(nullptr);
        app.action_kind = ActionKind::Launch;
        app.execution_payload = "km";
        return {app};
    }
};

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

bool close_to(const char* input, double expected) {
    const auto conversion = convert_units(input);
    return conversion && std::fabs(conversion->result - expected) < 1e-6 * std::max(1.0, std::fabs(expected));
}

}  // namespace

int main() {
    // Length, with and without a space, in or to, names and plurals
    assert(close_to("120 km in miles", 74.5645));
    assert(close_to("120km to mi", 74.5645));
    assert(close_to("1 mile to km", 1.609344));
    assert(close_to("12 in to cm", 30.48));
    assert(close_to("2 in in cm", 5.08));
    assert(close_to("6 feet to inches", 72.0));
    assert(close_to("1 metre to yd", 1.0936133));
    assert(close_to("+5 km to m", 5000.0));

    // Mass, time, volume, speed
    assert(close_to("1 lb to oz", 16.0));
    assert(close_to("2 kg to lbs", 4.4092452));
    assert(close_to("1 week to hours", 168.0));
    assert(close_to("90 min to h", 1.5));
    assert(close_to("1 gal to l", 3.785411784));
    assert(close_to("100 km/h to mph", 62.1371192));
    assert(close_to("10 m/s to km/h", 36.0));

    // Temperature, negatives included
    assert(close_to("100 c to f", 212.0));
    assert(close_to("32 °F to °C", 0.0));
    assert(close_to("-40 c to f", -40.0));
    assert(close_to("0 celsius to kelvin", 273.15));
    assert(close_to("300 K to C", 26.85));
    assert(close_to("-5 fahrenheit to c", -20.5555556));
    // Absolute zero is exactly zero, not leftover rounding noise from the offsets
    assert(convert_units("-459.67 f to k")->result == 0.0);
    assert(convert_units("-273.15 c to k")->result == 0.0);

    // Data: decimal, binary, bits versus bytes (Mb is megabits, any other casing of MB is megabytes)
    assert(close_to("1 KB to B", 1000.0));
    assert(close_to("1 KiB to B", 1024.0));
    assert(close_to("1 GiB to MiB", 1024.0));
    assert(close_to("1 GB to MB", 1000.0));
    assert(close_to("1 B to b", 8.0));
    assert(close_to("8 Mb to MB", 1.0));
    assert(close_to("1 MB to Mb", 8.0));
    assert(close_to("1 mb to MB", 1.0));
    assert(close_to("1 byte to bits", 8.0));

    // Unknown units, mixed kinds and ordinary sentences give nothing
    assert(!convert_units("5 foo to bar"));
    assert(!convert_units("5 km to foo"));
    assert(!convert_units("5 km to kg"));
    assert(!convert_units("5 c to km"));
    assert(!convert_units("log in"));
    assert(!convert_units("sign in to github"));
    assert(!convert_units("2 in"));
    assert(!convert_units("2 in cm"));
    assert(!convert_units("km to mi"));
    assert(!convert_units("1.2.3 km to mi"));
    assert(!convert_units("5 km to mi please"));
    assert(!convert_units("5 km into mi"));
    assert(!convert_units(""));
    assert(!convert_units("-"));
    assert(!convert_units("+"));
    assert(!convert_units("+-5 km to m"));
    assert(!convert_units("++5 km to m"));

    // Number formatting: no exponents for ordinary sizes, no trailing zeros
    assert(format_unit_value(1000000.0) == "1000000");
    assert(format_unit_value(1e12) == "1000000000000");
    assert(format_unit_value(74.5645) == "74.5645");
    assert(format_unit_value(74.56454321) == "74.5645");
    assert(format_unit_value(0.0123456789) == "0.0123457");
    assert(format_unit_value(-40.0) == "-40");
    assert(format_unit_value(1.5) == "1.5");
    assert(format_unit_value(0.0) == "0");
    // Tiny values keep their digits instead of showing a false 0
    assert(format_unit_value(-1e-15) == "-0.000000000000001");
    assert(format_unit_value(1.25e-13) == "0.000000000000125");
    assert(format_unit_value(convert_units("1 b to TB")->result) == "0.000000000000125");
    assert(format_unit_value(convert_units("1 mm to mi")->result) == "0.000000621371");

    // Provider result: value as title, full conversion as subtitle, Enter copies the value
    UnitProvider provider;
    const std::atomic<bool> no_cancel{false};
    const auto hits = provider.search(Query("120 km in miles"), no_cancel);
    assert(hits.size() == 1);
    assert(hits[0].title == "74.5645");
    assert(hits[0].subtitle == "120 km = 74.5645 miles");
    assert(hits[0].action_kind == ActionKind::Copy);
    assert(hits[0].execution_payload == "74.5645");
    assert(provider.search(Query("5 km to foo"), no_cancel).empty());

    // A comma locale must not turn the title into 1,50000 (that text is what gets copied)
    const std::string previous_locale = std::setlocale(LC_NUMERIC, nullptr);
    if (use_comma_locale()) {
        assert(format_unit_value(1.5) == "1.5");
        assert(format_unit_value(30.0) == "30");
        assert(format_unit_value(-1e-15) == "-0.000000000000001");
        const auto local_hits = provider.search(Query("90 min to h"), no_cancel);
        assert(local_hits.size() == 1);
        assert(local_hits[0].title == "1.5");
        assert(local_hits[0].subtitle == "90 min = 1.5 h");
        assert(close_to("1.5 km to m", 1500.0));
    } else {
        std::cout << "No comma-decimal locale installed, skipping the locale check\n";
    }
    std::setlocale(LC_NUMERIC, previous_locale.c_str());

    const std::atomic<bool> cancelled{true};
    assert(provider.search(Query("120 km in miles"), cancelled).empty());

    // With the calculator beside it: the calculator adds nothing for a conversion, and the
    // conversion leads even against a heavily used rival
    nexus::core::SearchEngine engine;
    engine.register_provider(std::make_shared<nexus::providers::CalculatorProvider>());
    engine.register_provider(std::make_shared<UnitProvider>());
    engine.register_provider(std::make_shared<HeavyAppProvider>());
    for (const char* input : {"120 km in miles", "-5 c to f", "100 km/h to mph"}) {
        const auto results = engine.search(Query(input));
        for (const auto& res : results) {
            assert(res.provider_id != "calculator");
        }
        assert(!results.empty());
        assert(results[0].provider_id == "units");
    }
    // Plain arithmetic still belongs to the calculator
    nexus::core::SearchEngine plain;
    plain.register_provider(std::make_shared<nexus::providers::CalculatorProvider>());
    plain.register_provider(std::make_shared<UnitProvider>());
    const auto sum = plain.search(Query("2+2"));
    assert(!sum.empty() && sum[0].provider_id == "calculator");

    return 0;
}
