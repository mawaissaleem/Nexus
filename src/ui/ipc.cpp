#include "nexus/ui/ipc.hpp"
#include <QLocalSocket>
#include <cstdlib>
#include <filesystem>
#include <unistd.h>

namespace nexus::ui {

QString ipc_socket_name_for(const char* runtime_dir, unsigned uid) {
    std::error_code ec;
    const std::filesystem::path dir = runtime_dir ? runtime_dir : "";
    if (dir.is_absolute() && std::filesystem::is_directory(dir, ec)) {
        return QString::fromStdString((dir / "nexus_launcher_ipc").string());
    }
    return QString("nexus_launcher_ipc_%1").arg(uid);
}

QString ipc_socket_name() { return ipc_socket_name_for(std::getenv("XDG_RUNTIME_DIR"), getuid()); }

const char* ipc_command_for_args(const std::vector<std::string>& args) {
    for (const std::string& arg : args) {
        if (arg == "--hide") return "HIDE";
        if (arg == "--show") return "SHOW";
        if (arg == "--settings" || arg == "settings" || arg == "config") return kIpcSettings;
        if (arg == "--toggle" || arg == "-t") return "TOGGLE";
    }
    for (const std::string& arg : args) {
        if (arg == "-session") return kIpcNoop;
    }
    return "TOGGLE";
}

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

}  // namespace nexus::ui
