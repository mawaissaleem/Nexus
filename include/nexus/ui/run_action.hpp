#pragma once

#include "nexus/core/result.hpp"
#include "nexus/core/search_provider.hpp"
#include <functional>
#include <vector>

namespace nexus::ui {

// Asks the user whether to go ahead with `res` (true = yes).
using ConfirmFn = std::function<bool(const core::SearchResult&)>;

// Default ConfirmFn: modal yes/no QMessageBox whose default button is No, so a stray Enter never confirms.
bool ask_yes_no(const core::SearchResult& res);

// Runs a result: asks `confirm` first when res.confirm is set, then dispatches and copies.
// Returns false (and does nothing) when the user said no.
bool run_action(const core::SearchResult& res, const std::vector<core::SearchProviderPtr>& providers,
                const ConfirmFn& confirm);

}  // namespace nexus::ui
