#pragma once

#include "nexus/providers/clipboard_provider.hpp"
#include <QString>

namespace nexus::ui {

inline constexpr const char* kIpcSocketName = "nexus_launcher_ipc";
inline constexpr const char* kIpcClearClipboard = "CLEAR_CLIPBOARD";

// Single-instance client: hands the fixed command `cmd` to the instance listening on `socket_name`.
// False when none is listening.
bool send_ipc_command(const QString& socket_name, const char* cmd);

// Server side of the commands that touch services rather than windows. True when `msg` was one of them.
bool handle_ipc_clipboard_command(const QString& msg, providers::ClipboardProvider& clipboard);

} // namespace nexus::ui
