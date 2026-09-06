#include "nexus/providers/alias_provider.hpp"
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/executor.hpp"
#include "nexus/utils/fuzzy.hpp"
#include <sstream>

namespace nexus::providers {

namespace {

std::string get_icon_for_type(core::AliasType type) {
    switch (type) {
        case core::AliasType::Url: return "web-browser";
        case core::AliasType::Directory: return "folder";
        case core::AliasType::File: return "text-x-generic";
        case core::AliasType::Command: return "utilities-terminal";
    }
    return "emblem-favorite";
}

} // namespace

std::vector<core::SearchResult> AliasProvider::search(
    const core::Query& query,
    const std::atomic<bool>& cancel_token
) {
    std::vector<core::SearchResult> results;
    if (cancel_token.load() || query.empty()) {
        return results;
    }

    std::string raw = query.raw();

    // 1. Inline creation mode: "alias <name> <target>"
    if (raw.rfind("alias ", 0) == 0) {
        std::istringstream iss(raw.substr(6));
        std::string name, target;
        if (iss >> name >> target) {
            core::SearchResult create_res;
            create_res.id = "alias:create:" + name;
            create_res.title = "Save alias '" + name + "' → " + target;
            create_res.subtitle = "Press Enter to create this nickname shortcut";
            create_res.icon = "emblem-favorite";
            create_res.provider_id = id();
            create_res.score = 2500.0;
            create_res.action = [name, target]() {
                return core::AliasManager::instance().add_alias(name, target);
            };
            results.push_back(std::move(create_res));
            return results;
        }
    }

    // 2. Normal matching against existing aliases
    const auto& search_term = query.normalized();
    auto aliases = core::AliasManager::instance().get_aliases();

    for (const auto& alias : aliases) {
        if (cancel_token.load()) return {};

        std::string alias_name_lower = utils::FuzzyMatcher::to_lower(alias.name);
        double score = 0.0;
        std::vector<size_t> matched_indices;

        if (alias_name_lower == search_term) {
            score = 1600.0;
            for (size_t i = 0; i < alias.name.size(); ++i) matched_indices.push_back(i);
        } else if (utils::FuzzyMatcher::istarts_with(alias_name_lower, search_term)) {
            score = 950.0;
            for (size_t i = 0; i < search_term.size(); ++i) matched_indices.push_back(i);
        } else {
            auto match = utils::FuzzyMatcher::match(search_term, alias.name);
            if (match.matched) {
                score = match.score + 250.0;
                matched_indices = match.matched_indices;
            }
        }

        if (score > 0.0) {
            core::SearchResult res;
            res.id = "alias:" + alias.name;
            res.title = alias.name;
            res.subtitle = "Alias → " + alias.target;
            res.icon = get_icon_for_type(alias.type);
            res.provider_id = id();
            res.score = score;
            res.matched_indices = std::move(matched_indices);
            res.execution_payload = alias.target;

            std::string target_str = alias.target;
            core::AliasType type = alias.type;

            res.action = [target_str, type]() {
                if (type == core::AliasType::Command) {
                    return core::Executor::launch_shell_command(target_str);
                } else {
                    return core::Executor::open_path_or_url(target_str);
                }
            };

            results.push_back(std::move(res));
        }
    }

    return results;
}

} // namespace nexus::providers
