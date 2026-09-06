#include "nexus/providers/app_provider.hpp"
#include "nexus/core/executor.hpp"
#include "nexus/utils/fuzzy.hpp"

namespace nexus::providers {

ApplicationProvider::ApplicationProvider(
    std::shared_ptr<index::AppIndexer> indexer,
    std::shared_ptr<index::Database> db
) : indexer_(std::move(indexer)), db_(std::move(db)) {}

std::vector<core::SearchResult> ApplicationProvider::search(
    const core::Query& query,
    const std::atomic<bool>& cancel_token
) {
    std::vector<core::SearchResult> results;
    if (!indexer_ || query.mode() != core::QueryMode::General) {
        return results;
    }

    const auto& search_term = query.normalized();
    if (search_term.empty()) {
        return results;
    }

    auto apps = indexer_->get_applications();

    for (const auto& app : apps) {
        if (cancel_token.load()) {
            return {};
        }

        // Fuzzy match on Name
        auto name_match = utils::FuzzyMatcher::match(search_term, app.name);
        double best_score = name_match.matched ? name_match.score : 0.0;
        std::vector<size_t> matched_indices = name_match.matched_indices;

        // Also check GenericName
        if (!app.generic_name.empty()) {
            auto gen_match = utils::FuzzyMatcher::match(search_term, app.generic_name);
            if (gen_match.matched && (gen_match.score * 0.8) > best_score) {
                best_score = gen_match.score * 0.8;
            }
        }

        // Also check Keywords
        for (const auto& kw : app.keywords) {
            auto kw_match = utils::FuzzyMatcher::match(search_term, kw);
            if (kw_match.matched && (kw_match.score * 0.75) > best_score) {
                best_score = kw_match.score * 0.75;
            }
        }

        // Also check Comment
        if (best_score == 0.0 && !app.comment.empty()) {
            auto comment_match = utils::FuzzyMatcher::match(search_term, app.comment);
            if (comment_match.matched) {
                best_score = comment_match.score * 0.5;
            }
        }

        if (best_score > 0.0) {
            core::SearchResult res;
            res.id = app.id;
            res.title = app.name;
            res.subtitle = !app.generic_name.empty() ? app.generic_name : app.comment;
            res.icon = app.icon;
            res.provider_id = id();
            res.score = best_score;
            res.matched_indices = std::move(matched_indices);
            res.execution_payload = app.exec;

            std::string exec_cmd = app.exec;
            bool terminal = app.terminal;
            std::string app_id = app.id;
            std::string raw_q = query.raw();
            auto db_ref = db_;

            res.action = [exec_cmd, terminal, app_id, raw_q, db_ref]() -> bool {
                if (db_ref && db_ref->is_open()) {
                    db_ref->record_usage(app_id, raw_q);
                    db_ref->record_query(raw_q, app_id, "applications");
                }
                return core::Executor::launch_desktop_exec(exec_cmd, terminal);
            };

            if (db_ && db_->is_open()) {
                int usage = db_->get_usage_count(app.id);
                if (usage > 0) {
                    res.metadata["usage_count"] = std::to_string(usage);
                }
            }

            res.metadata["desktop_file"] = app.desktop_file;
            results.push_back(std::move(res));
        }
    }

    return results;
}

} // namespace nexus::providers

