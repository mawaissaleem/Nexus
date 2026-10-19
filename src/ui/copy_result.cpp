#include "nexus/ui/copy_result.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <QString>

namespace nexus::ui {

void copy_result(const core::SearchResult& res) {
    if (res.action_kind != core::ActionKind::Copy) return;
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(QString::fromStdString(res.execution_payload));
    }
}

}  // namespace nexus::ui
