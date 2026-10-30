#include "nexus/ui/ipc.hpp"
#include <QLocalSocket>

namespace nexus::ui {

bool send_ipc_command(const QString& socket_name, const char* cmd) {
    QLocalSocket socket;
    socket.connectToServer(socket_name);
    if (!socket.waitForConnected(200)) return false;
    socket.write(cmd);
    socket.flush();
    socket.waitForBytesWritten(200);
    return true;
}

bool handle_ipc_clipboard_command(const QString& msg, providers::ClipboardProvider& clipboard) {
    if (msg != kIpcClearClipboard) return false;
    clipboard.clear();
    return true;
}

} // namespace nexus::ui
