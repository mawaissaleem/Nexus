#include "nexus/core/executor.hpp"
#include "nexus/utils/logger.hpp"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdlib>

namespace nexus::core {

std::string Executor::clean_desktop_exec(std::string_view exec_line) {
    std::string result;
    result.reserve(exec_line.size());

    bool in_quotes = false;
    for (size_t i = 0; i < exec_line.size(); ++i) {
        char c = exec_line[i];
        if (c == '"') {
            in_quotes = !in_quotes;
            result.push_back(c);
            continue;
        }

        if (c == '%' && i + 1 < exec_line.size()) {
            char next = exec_line[i + 1];
            // Recognized field codes in Freedesktop Desktop Entry Specification
            if (strchr("fFuUdDnNikvm", next) != nullptr) {
                ++i; // skip '%' and code
                continue;
            }
        }
        result.push_back(c);
    }

    // Trim trailing whitespace
    while (!result.empty() && std::isspace(static_cast<unsigned char>(result.back()))) {
        result.pop_back();
    }
    return result;
}

bool Executor::launch_desktop_exec(std::string_view exec_line, bool terminal) {
    std::string cleaned = clean_desktop_exec(exec_line);
    if (cleaned.empty()) {
        NEXUS_LOG_WARN("Cannot launch empty desktop command");
        return false;
    }

    if (terminal) {
        cleaned = "x-terminal-emulator -e " + cleaned;
    }

    return launch_shell_command(cleaned);
}

bool Executor::launch_shell_command(std::string_view command) {
    if (command.empty()) return false;

    std::string cmd_str(command);
    NEXUS_LOG_INFO("Executing command: " + cmd_str);

    pid_t pid = fork();
    if (pid < 0) {
        NEXUS_LOG_ERROR("fork() failed");
        return false;
    }

    if (pid == 0) {
        // Child process
        setsid(); // Detach from session

        pid_t second_pid = fork();
        if (second_pid < 0) {
            _exit(EXIT_FAILURE);
        }
        if (second_pid > 0) {
            // First child exits immediately so grandchild is adopted by init/systemd
            _exit(EXIT_SUCCESS);
        }

        // Grandchild process
        // Redirect standard descriptors to /dev/null
        if (freopen("/dev/null", "r", stdin) == nullptr ||
            freopen("/dev/null", "w", stdout) == nullptr ||
            freopen("/dev/null", "w", stderr) == nullptr) {
            // Ignore failure, proceed
        }

        execl("/bin/sh", "sh", "-c", cmd_str.c_str(), static_cast<char*>(nullptr));
        _exit(EXIT_FAILURE);
    }

    // Parent waits for the first child to terminate
    int status = 0;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool Executor::open_path_or_url(std::string_view target) {
    if (target.empty()) return false;
    std::string command = "xdg-open \"" + std::string(target) + "\"";
    return launch_shell_command(command);
}

} // namespace nexus::core

