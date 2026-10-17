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
#include <utility>
#include <algorithm>
#include <csignal>
#include <mutex>
#include <poll.h>
#include <signal.h>

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
        signal(SIGPIPE, SIG_DFL);  // Nexus ignores SIGPIPE (spawn_with_pipes); commands expect the default
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

    sigset_t sigpipe_only;
    sigemptyset(&sigpipe_only);
    sigaddset(&sigpipe_only, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &sigpipe_only);  // Nexus ignores SIGPIPE; apps expect the default
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSID | POSIX_SPAWN_SETSIGDEF);

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

ChildProcess::ChildProcess(ChildProcess&& o) noexcept
    : pid_(std::exchange(o.pid_, -1)), in_(std::exchange(o.in_, -1)), out_(std::exchange(o.out_, -1)),
      buf_(std::move(o.buf_)) {}

ChildProcess& ChildProcess::operator=(ChildProcess&& o) noexcept {
    if (this != &o) {
        terminate(std::chrono::milliseconds(0));
        pid_ = std::exchange(o.pid_, -1);
        in_ = std::exchange(o.in_, -1);
        out_ = std::exchange(o.out_, -1);
        buf_ = std::move(o.buf_);
    }
    return *this;
}

bool ChildProcess::write_line(std::string_view line) {
    if (in_ < 0) return false;
    size_t done = 0;
    while (done < line.size()) {
        const ssize_t n = write(in_, line.data() + done, line.size() - done);
        if (n < 0 && errno == EINTR) continue;
        if (n < 0) return false;  // EAGAIN (pipe full), EPIPE (child gone), ...
        done += static_cast<size_t>(n);
        if (done < line.size()) return false;  // partial line written: stream is desynchronised
    }
    return true;
}

ChildProcess::ReadStatus ChildProcess::read_line(std::string& line, std::chrono::steady_clock::time_point deadline,
                                                 const std::atomic<bool>& cancel, std::size_t max_bytes) {
    using clock = std::chrono::steady_clock;
    bool polled = false;
    for (;;) {
        const size_t nl = buf_.find('\n');
        if (nl != std::string::npos) {
            if (nl > max_bytes) return ReadStatus::TooLong;
            line.assign(buf_, 0, nl);
            buf_.erase(0, nl + 1);
            return ReadStatus::Line;
        }
        if (buf_.size() > max_bytes) return ReadStatus::TooLong;
        if (cancel.load()) return ReadStatus::Cancelled;
        if (out_ < 0) return ReadStatus::Closed;

        // Checked on every pass, not only when poll() times out: a trickling child must not hold us past it.
        if (polled && clock::now() >= deadline) return ReadStatus::Timeout;
        polled = true;
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - clock::now());
        const int slice = static_cast<int>(std::clamp<long long>(left.count(), 0, 5));
        pollfd p{out_, POLLIN, 0};
        const int rc = poll(&p, 1, slice);
        if (rc < 0 && errno != EINTR) return ReadStatus::Closed;
        if (rc > 0) {
            char chunk[4096];
            const ssize_t n = read(out_, chunk, sizeof chunk);
            if (n == 0) return ReadStatus::Closed;
            if (n < 0 && errno != EINTR && errno != EAGAIN) return ReadStatus::Closed;
            if (n > 0) buf_.append(chunk, static_cast<size_t>(n));
        }
    }
}

void ChildProcess::terminate(std::chrono::milliseconds grace) {
    if (in_ >= 0) close(in_);
    if (out_ >= 0) close(out_);
    in_ = out_ = -1;
    if (pid_ <= 0) return;
    const auto until = std::chrono::steady_clock::now() + grace;
    int st = 0;
    bool reaped = false;
    while (!reaped) {
        const pid_t r = waitpid(pid_, &st, WNOHANG);
        if (r == pid_ || (r < 0 && errno != EINTR)) { reaped = true; break; }
        if (std::chrono::steady_clock::now() >= until) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!reaped) {
        kill(pid_, SIGKILL);
        while (waitpid(pid_, &st, 0) < 0 && errno == EINTR) {}
    }
    pid_ = -1;
    buf_.clear();
}

ChildProcess Executor::spawn_with_pipes(const std::vector<std::string>& args, const std::filesystem::path& cwd) {
    if (args.empty()) return {};
    static std::once_flag sigpipe_once;
    std::call_once(sigpipe_once, [] { signal(SIGPIPE, SIG_IGN); });

    int to_child[2], from_child[2];
    if (pipe2(to_child, O_CLOEXEC) != 0) return {};
    if (pipe2(from_child, O_CLOEXEC) != 0) {
        close(to_child[0]); close(to_child[1]);
        return {};
    }
    int log_fd = cwd.empty() ? -1 : open((cwd / "plugin.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);

    std::vector<char*> argv;
    for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, to_child[0], STDIN_FILENO);
    posix_spawn_file_actions_adddup2(&actions, from_child[1], STDOUT_FILENO);
    if (log_fd >= 0) posix_spawn_file_actions_adddup2(&actions, log_fd, STDERR_FILENO);
    else posix_spawn_file_actions_addopen(&actions, STDERR_FILENO, "/dev/null", O_WRONLY, 0);
    if (!cwd.empty()) posix_spawn_file_actions_addchdir_np(&actions, cwd.c_str());

    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    sigset_t sigpipe_only;
    sigemptyset(&sigpipe_only);
    sigaddset(&sigpipe_only, SIGPIPE);
    posix_spawnattr_setsigdefault(&attr, &sigpipe_only);
    posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGDEF);

    pid_t pid = 0;
    const int rc = posix_spawn(&pid, argv[0], &actions, &attr, argv.data(), environ);
    posix_spawnattr_destroy(&attr);
    posix_spawn_file_actions_destroy(&actions);
    close(to_child[0]);
    close(from_child[1]);
    if (log_fd >= 0) close(log_fd);
    if (rc != 0) {
        close(to_child[1]);
        close(from_child[0]);
        return {};
    }
    fcntl(to_child[1], F_SETFL, fcntl(to_child[1], F_GETFL) | O_NONBLOCK);
    return ChildProcess(pid, to_child[1], from_child[0]);
}

bool Executor::open_path_or_url(std::string_view target) {
    if (target.empty()) return false;
    return spawn_detached({"xdg-open", std::string(target)});
}

} // namespace nexus::core

