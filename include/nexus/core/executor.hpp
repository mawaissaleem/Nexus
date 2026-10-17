#pragma once

#include <atomic>
#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <sys/types.h>

namespace nexus::core {

// A child process with a line-oriented pipe on each side (see Executor::spawn_with_pipes).
// Move-only; the destructor kills and reaps a process that is still running.
class ChildProcess {
public:
    enum class ReadStatus { Line, Timeout, Cancelled, Closed, TooLong };

    ChildProcess() = default;
    ChildProcess(pid_t pid, int stdin_fd, int stdout_fd) noexcept : pid_(pid), in_(stdin_fd), out_(stdout_fd) {}
    ChildProcess(ChildProcess&& o) noexcept;
    ChildProcess& operator=(ChildProcess&& o) noexcept;
    ChildProcess(const ChildProcess&) = delete;
    ChildProcess& operator=(const ChildProcess&) = delete;
    ~ChildProcess() { terminate(std::chrono::milliseconds(0)); }

    [[nodiscard]] bool valid() const noexcept { return pid_ > 0; }

    // Writes `line` (the caller includes the '\n') without ever blocking. false if the pipe is full, closed
    // or only part of the line fit; the stream is then unusable and the caller should terminate().
    bool write_line(std::string_view line);

    // Next '\n'-terminated line (without it) from stdout. Waits in poll() slices of at most 5 ms until
    // `deadline` or until `cancel` is true. Partial data stays buffered for the next call. Closed = EOF or
    // error; TooLong = a line over `max_bytes`. An already buffered line is returned even past the deadline,
    // so a caller looping over lines must bound the loop itself; a past deadline still polls once (no wait).
    ReadStatus read_line(std::string& line, std::chrono::steady_clock::time_point deadline,
                         const std::atomic<bool>& cancel, std::size_t max_bytes = 64 * 1024);

    // Closes stdin, waits up to `grace` for the child to exit, then SIGKILLs it, and reaps it.
    void terminate(std::chrono::milliseconds grace);

private:
    pid_t pid_{-1};
    int in_{-1};
    int out_{-1};
    std::string buf_;  // bytes read from stdout but not yet returned as a line
};

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

    // Runs argv[0] (an exact path, no PATH search, no shell) in `cwd` with stdin/stdout on pipes and stderr
    // appended to "<cwd>/plugin.log" (truncated; /dev/null if that can't be opened). Ignores SIGPIPE
    // process-wide once, so writing to a dead child fails with EPIPE instead of killing Nexus (every child
    // Nexus starts gets SIGPIPE back at its default).
    // Returns an invalid ChildProcess on failure.
    static ChildProcess spawn_with_pipes(const std::vector<std::string>& argv, const std::filesystem::path& cwd);

private:
    // posix_spawnp argv[0] with stdio on /dev/null, new session, and a detached reaper.
    static bool spawn_detached(const std::vector<std::string>& args);
};

} // namespace nexus::core
