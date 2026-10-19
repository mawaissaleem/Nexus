// copy_result puts a Copy result's payload on the real (offscreen) Qt clipboard.
#include "nexus/ui/copy_result.hpp"
#include <QClipboard>
#include <QGuiApplication>
#include <cassert>

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    nexus::core::SearchResult res;
    res.action_kind = nexus::core::ActionKind::Copy;
    res.execution_payload = "hello clipboard";
    nexus::ui::copy_result(res);
    assert(QGuiApplication::clipboard()->text() == "hello clipboard");

    // Non-Copy results must leave the clipboard alone.
    res.action_kind = nexus::core::ActionKind::Open;
    res.execution_payload = "/tmp/other";
    nexus::ui::copy_result(res);
    assert(QGuiApplication::clipboard()->text() == "hello clipboard");
    return 0;
}
