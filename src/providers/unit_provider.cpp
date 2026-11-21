#include "nexus/providers/unit_provider.hpp"
#include "nexus/providers/unit_converter.hpp"

namespace nexus::providers {

std::vector<core::SearchResult> UnitProvider::search(const core::Query& query, const std::atomic<bool>& cancel_token) {
    if (cancel_token.load() || !query.prefix().empty()) {
        return {};
    }

    const auto conversion = convert_units(query.text());
    if (!conversion) {
        return {};
    }

    const std::string value = format_unit_value(conversion->result);
    const std::string from_value = format_unit_value(conversion->input);

    core::SearchResult res;
    res.id = "unit:" + value;
    res.title = value;
    res.subtitle = from_value + " " + conversion->from + " = " + value + " " + conversion->to;
    res.icon = "accessories-calculator";
    res.provider_id = id();
    res.score = 2000.0;  // the whole input is a conversion, so it should lead like a URL does
    res.execution_payload = value;
    res.action_kind = core::ActionKind::Copy;
    return {res};
}

}  // namespace nexus::providers
