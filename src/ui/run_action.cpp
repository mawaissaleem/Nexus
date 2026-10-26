#include "nexus/ui/run_action.hpp"
#include "nexus/core/action_dispatcher.hpp"
#include "nexus/ui/copy_result.hpp"
#include <QMessageBox>

namespace nexus::ui {

bool ask_yes_no(const core::SearchResult& res) {
    const auto text = QString::fromStdString("Run \"" + res.title + "\"?");
    return QMessageBox::question(nullptr, "Nexus", text, QMessageBox::Yes | QMessageBox::No, QMessageBox::No) ==
           QMessageBox::Yes;
}

bool run_action(const core::SearchResult& res, const std::vector<core::SearchProviderPtr>& providers,
                const ConfirmFn& confirm) {
    if (res.confirm && !confirm(res)) return false;
    core::dispatch_action(res, providers);
    copy_result(res);  // dispatcher treats Copy as handled; the clipboard is Qt, so it happens here
    return true;
}

}  // namespace nexus::ui
