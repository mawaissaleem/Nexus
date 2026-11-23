#pragma once

#include "nexus/providers/clipboard_provider.hpp"
#include <QString>
#include <string>
#include <vector>

namespace nexus::ui {

inline constexpr const char* kIpcSocketName = "nexus_launcher_ipc";
inline constexpr const char* kIpcClearClipboard = "CLEAR_CLIPBOARD";
inline constexpr const char* kIpcSettings = "SETTINGS";
inline constexpr const char* kIpcNoop = "NOOP";  // the server ignores it: used to hand over without any effect

// The command a second start sends to a running instance, found anywhere in the arguments because Qt options
// (-style fusion, -platform xcb) can come first. The first command wins; a session-restore start (-session)
// sends the no-op so it neither toggles nor shows anything; no command at all means toggle.
const char* ipc_command_for_args(const std::vector<std::string>& args);

// Single-instance client: hands the fixed command `cmd` to the instance listening on `socket_name`.
// False when none is listening.
bool send_ipc_command(const QString& socket_name, const char* cmd);

// Server side of the commands that touch services rather than windows. True when `msg` was one of them.
bool handle_ipc_clipboard_command(const QString& msg, providers::ClipboardProvider& clipboard);

}  // namespace nexus::ui
