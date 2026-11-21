#pragma once

#include "nexus/core/executor.hpp"
#include "nexus/core/result.hpp"
#include "nexus/core/search_provider.hpp"
#include <functional>
#include <string>
#include <vector>

namespace nexus::ui {

// Asks the user whether to go ahead with `res` (true = yes).
using ConfirmFn = std::function<bool(const core::SearchResult&)>;

// The text ask_yes_no shows: the title as the question, the subtitle below it for context.
[[nodiscard]] std::string confirm_question(const core::SearchResult& res);

// Default ConfirmFn: modal yes/no QMessageBox whose default button is No, so a stray Enter never confirms.
bool ask_yes_no(const core::SearchResult& res);

// Runs a result: asks `confirm` first when res.confirm is set, then dispatches and copies.
// Returns false (and does nothing) when the user said no. `on_shell_failure` is passed on to the dispatcher.
bool run_action(const core::SearchResult& res, const std::vector<core::SearchProviderPtr>& providers,
                const ConfirmFn& confirm, core::ShellFailureFn on_shell_failure = {});

}  // namespace nexus::ui
