#include "nexus/ui/run_action.hpp"
#include "nexus/core/action_dispatcher.hpp"
#include "nexus/ui/copy_result.hpp"
#include <QMessageBox>

namespace nexus::ui {

std::string confirm_question(const core::SearchResult& res) {
    const std::string question = "Run \"" + res.title + "\"?";
    // A shell alias's title is only its name; its subtitle names the command that will run.
    if (res.subtitle.empty()) return question;
    return question + "\n\n" + res.subtitle;
}

bool ask_yes_no(const core::SearchResult& res) {
    const auto text = QString::fromStdString(confirm_question(res));
    return QMessageBox::question(nullptr, "Nexus", text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
           QMessageBox::Yes;
}

bool run_action(const core::SearchResult& res, const std::vector<core::SearchProviderPtr>& providers,
                const ConfirmFn& confirm, core::ShellFailureFn on_shell_failure) {
    if (res.confirm && !confirm(res)) return false;
    core::dispatch_action(res, providers, std::move(on_shell_failure));
    copy_result(res);  // dispatcher treats Copy as handled; the clipboard is Qt, so it happens here
    return true;
}

}  // namespace nexus::ui
