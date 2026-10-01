#include "nexus/core/executor.hpp"
#include "nexus/utils/logger.hpp"
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <spawn.h>
#include <fcntl.h>
#include <sstream>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <system_error>
#include <thread>
#include <optional>

extern char **environ;

namespace nexus::core {

std::optional<std::vector<std::string>> Executor::parse_desktop_exec(std::string_view exec_line) {
    // Pass 1: the "string" escapes of the Desktop Entry spec (the file loader keeps values raw).
    std::string text;
    for (size_t i = 0; i < exec_line.size(); ++i) {
        char c = exec_line[i];
        if (c == '\\' && i + 1 < exec_line.size()) {
            const char n = exec_line[i + 1];
            if (n == 's') { text.push_back(' '); ++i; continue; }
            if (n == 'n') { text.push_back('\n'); ++i; continue; }
            if (n == 't') { text.push_back('\t'); ++i; continue; }
            if (n == 'r') { text.push_back('\r'); ++i; continue; }
            if (n == '\\') { text.push_back('\\'); ++i; continue; }
        }
        text.push_back(c);
    }

    // Pass 2: the Exec quoting rules, plus field-code handling.
    std::vector<std::string> args;
    std::string cur;
    bool has_arg = false;   // a quoted or literal char was seen (field codes alone don't count)
    bool in_quotes = false;
    for (size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (in_quotes) {
            if (c == '"') { in_quotes = false; continue; }
            if (c == '\\' && i + 1 < text.size() && strchr("\"`$\\", text[i + 1])) {
                cur.push_back(text[++i]);
                continue;
            }
        } else {
            if (c == '"') { in_quotes = true; has_arg = true; continue; }
            if (c == ' ' || c == '\t' || c == '\n') {
                if (has_arg) args.push_back(std::move(cur));
                cur.clear();
                has_arg = false;
                continue;
            }
        }
        if (c == '%' && i + 1 < text.size()) {
            const char n = text[i + 1];
            if (n == '%') { cur.push_back('%'); has_arg = true; ++i; continue; }
            // Nexus never passes files/URLs, so every known field code expands to nothing.
            if (strchr("fFuUdDnNikvmc", n) != nullptr) { ++i; continue; }
        }
        cur.push_back(c);
        has_arg = true;
    }
    if (in_quotes) return std::nullopt;
    if (has_arg) args.push_back(std::move(cur));
    if (args.empty() || args.front().empty()) return std::nullopt;
    return args;
}

bool Executor::launch_desktop_exec(std::string_view exec_line, bool terminal) {
    auto args = parse_desktop_exec(exec_line);
    if (!args) {
        NEXUS_LOG_WARN("Cannot launch malformed or empty desktop command");
        return false;
    }
    if (terminal) {
        // argv form of -e (no shell); works with xterm-style emulators.
        args->insert(args->begin(), {"x-terminal-emulator", "-e"});
    }
    return spawn_detached(*args);
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

bool Executor::spawn_detached(const std::vector<std::string>& args) {
    if (args.empty()) return false;

    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    posix_spawnattr_t attr;
    posix_spawn_file_actions_t actions;

    if (posix_spawnattr_init(&attr) != 0 || posix_spawn_file_actions_init(&actions) != 0) {
        if (posix_spawnattr_init(&attr) == 0) {
            posix_spawnattr_destroy(&attr);
        }
        if (posix_spawn_file_actions_init(&actions) == 0) {
            posix_spawn_file_actions_destroy(&actions);
        }
        return false;
    }

    int stdin_fd = open("/dev/null", O_RDONLY);
    int stdout_fd = open("/dev/null", O_WRONLY);
    int stderr_fd = open("/dev/null", O_WRONLY);

    if (stdin_fd >= 0) {
        posix_spawn_file_actions_adddup2(&actions, stdin_fd, STDIN_FILENO);
    }
    if (stdout_fd >= 0) {
        posix_spawn_file_actions_adddup2(&actions, stdout_fd, STDOUT_FILENO);
    }
    if (stderr_fd >= 0) {
        posix_spawn_file_actions_adddup2(&actions, stderr_fd, STDERR_FILENO);
    }

    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID);

    pid_t pid = 0;
    int rc = posix_spawnp(&pid, argv[0], &actions, &attr, argv.data(), environ);

    if (stdin_fd >= 0) close(stdin_fd);
    if (stdout_fd >= 0) close(stdout_fd);
    if (stderr_fd >= 0) close(stderr_fd);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    if (rc != 0) return false;
    // Reap in a detached waiter so the child never lingers as a zombie.
    try {
        std::thread([pid] {
            int st = 0;
            while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
        }).detach();
    } catch (const std::system_error&) {
        // No thread available: reap only if already done; never block the UI thread.
        int st = 0;
        waitpid(pid, &st, WNOHANG);
        NEXUS_LOG_WARN("spawn_detached: could not start reaper thread; child may linger as a zombie");
    }
    return true;
}

bool Executor::open_path_or_url(std::string_view target) {
    if (target.empty()) return false;
    return spawn_detached({"xdg-open", std::string(target)});
}

} // namespace nexus::core

