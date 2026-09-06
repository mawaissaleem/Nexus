#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nexus::core {

class Executor {
public:
    // Execute a desktop application Exec string (with argument expansion / cleanup)
    static bool launch_desktop_exec(std::string_view exec_line, bool terminal = false);

    // Execute arbitrary shell command via /bin/sh or bash in detached mode
    static bool launch_shell_command(std::string_view command);

    // Open a file or URL with default system handler (xdg-open)
    static bool open_path_or_url(std::string_view target);

    // Clean an Exec line according to Freedesktop Desktop Entry Specification
    // Strips field codes (%f, %F, %u, %U, %d, %D, %n, %N, %i, %c, %k, %v, %m)
    static std::string clean_desktop_exec(std::string_view exec_line);
};

} // namespace nexus::core

