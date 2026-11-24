// IPC client and the clipboard-clear command. Uses a socket name unique to this process, never the launcher's.
#include "nexus/core/query.hpp"
#include "check.hpp"
#include "nexus/ui/ipc.hpp"
#include <QCoreApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QString>
#include <QTimer>
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>
#include <unistd.h>

using namespace nexus;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const QString name = QString("nexus_test_ipc_%1").arg(getpid());

    // Name choice is pure: a usable runtime dir wins, otherwise the name carries the uid. No socket is touched.
    const std::string run_dir = std::filesystem::temp_directory_path().string();
    CHECK_EQ(ui::ipc_socket_name_for(run_dir.c_str(), 1234).toStdString(), run_dir + "/nexus_launcher_ipc");
    CHECK_EQ(ui::ipc_socket_name_for(nullptr, 1234).toStdString(), std::string("nexus_launcher_ipc_1234"));
    CHECK_EQ(ui::ipc_socket_name_for("", 1234).toStdString(), std::string("nexus_launcher_ipc_1234"));
    CHECK_EQ(ui::ipc_socket_name_for("relative/dir", 7).toStdString(), std::string("nexus_launcher_ipc_7"));
    CHECK_EQ(ui::ipc_socket_name_for("/nonexistent_nexus_run_dir", 7).toStdString(),
             std::string("nexus_launcher_ipc_7"));
    assert(name != ui::ipc_socket_name_for(run_dir.c_str(), getuid()));
    assert(name != ui::ipc_socket_name_for(nullptr, getuid()));

    providers::ClipboardProvider clip(10);
    clip.add("hello");
    auto count = [&] {
        std::atomic<bool> cancel{false};
        return clip.search(core::Query("clip ", "clip "), cancel).size();
    };
    assert(count() == 1);

    // Handler: only the fixed command clears; anything else is ignored.
    assert(!ui::handle_ipc_clipboard_command("TOGGLE", clip) && count() == 1);
    assert(!ui::handle_ipc_clipboard_command("clear_clipboard; rm -rf", clip) && count() == 1);
    assert(ui::handle_ipc_clipboard_command(ui::kIpcClearClipboard, clip) && count() == 0);

    // Command scan: Qt options may come before, between or after it.
    using Args = std::vector<std::string>;
    assert(std::string(ui::ipc_command_for_args(Args{})) == "TOGGLE");
    assert(std::string(ui::ipc_command_for_args(Args{"-style", "fusion"})) == "TOGGLE");
    assert(std::string(ui::ipc_command_for_args(Args{"--hide"})) == "HIDE");
    assert(std::string(ui::ipc_command_for_args(Args{"-style", "fusion", "--settings"})) == "SETTINGS");
    assert(std::string(ui::ipc_command_for_args(Args{"--settings", "-platform", "xcb"})) == "SETTINGS");
    assert(std::string(ui::ipc_command_for_args(Args{"-platform", "xcb", "--show"})) == "SHOW");
    assert(std::string(ui::ipc_command_for_args(Args{"-session", "abc"})) == ui::kIpcNoop);

    // No server listening: the send reports false, nothing else happens.
    QLocalServer::removeServer(name);
    assert(!ui::send_ipc_command(name, ui::kIpcClearClipboard));

    // With a server: the exact message arrives.
    QLocalServer server;
    assert(server.listen(name));
    QString got;
    QObject::connect(&server, &QLocalServer::newConnection, [&] {
        QLocalSocket* c = server.nextPendingConnection();
        QObject::connect(c, &QLocalSocket::readyRead, [&got, c] { got = QString::fromUtf8(c->readAll()); });
    });
    // The client blocks briefly while the server needs the event loop, so send from a pumped timer.
    bool sent = false;
    QTimer::singleShot(0, [&] { sent = ui::send_ipc_command(name, ui::kIpcClearClipboard); });
    QTimer::singleShot(500, &app, &QCoreApplication::quit);
    app.exec();
    assert(sent && got == ui::kIpcClearClipboard);
    return 0;
}
