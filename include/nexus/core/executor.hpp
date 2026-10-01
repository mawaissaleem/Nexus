#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::core {

class Executor {
public:
    // Execute a desktop application Exec string (tokenised, run without a shell)
    static bool launch_desktop_exec(std::string_view exec_line, bool terminal = false);

    // Execute arbitrary shell command via /bin/sh or bash in detached mode
    static bool launch_shell_command(std::string_view command);

    // Open a file or URL with default system handler (xdg-open)
    static bool open_path_or_url(std::string_view target);

    // Tokenise a Desktop Entry Exec value into argv (spec quoting/escapes, field codes removed).
    // nullopt for an unterminated quote or an empty command.
    static std::optional<std::vector<std::string>> parse_desktop_exec(std::string_view exec_line);

private:
    // posix_spawnp argv[0] with stdio on /dev/null, new session, and a detached reaper.
    static bool spawn_detached(const std::vector<std::string>& args);
};

} // namespace nexus::core
