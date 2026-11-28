#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <atomic>
#include <csignal>
#include <fstream>
#include <iterator>
#include <iostream>
#include <mutex>
#include <thread>
#include <string>
#include <vector>
#include <unistd.h>
#include "check.hpp"
#include "nexus/core/executor.hpp"

using namespace nexus::core;

// True if this process has any child (live or zombie; scans /proc/*/stat).
static bool has_child() {
    for (const auto& e : std::filesystem::directory_iterator("/proc")) {
        std::ifstream f(e.path() / "stat");
        std::string line;
        if (!std::getline(f, line)) continue;
        auto p = line.rfind(')');  // comm may contain spaces
        if (p == std::string::npos) continue;
        char state = 0;
        long ppid = 0;
        if (sscanf(line.c_str() + p + 1, " %c %ld", &state, &ppid) == 2 && ppid == getpid()) return true;
    }
    return false;
}

int main() {
    using V = std::vector<std::string>;
    auto parse = [](const char* l) { return Executor::parse_desktop_exec(l); };

    // Field codes are removed (alone or inside an argument); %% is a literal percent.
    assert(*parse("/usr/bin/app %f --flag %U --name=%c %i") == (V{"/usr/bin/app", "--flag", "--name="}));
    assert(*parse("app 100%% %z") == (V{"app", "100%", "%z"}));
    assert(*parse("/usr/bin/app --flag") == (V{"/usr/bin/app", "--flag"}));
    // Quoting: spaces kept, quote escapes honoured, empty quoted arg kept.
    assert(*parse("\"/opt/my app/run\" -x \"a b\" \"\"") == (V{"/opt/my app/run", "-x", "a b", ""}));
    assert(*parse("app \"q\\\"\\$\\`\\\\\\\\\"") == (V{"app", "q\"$`\\"}));  // file text: "q\"\$\`\\\\"
    assert(*parse("app \"100%% %f\"") == (V{"app", "100% "}));
    // String-level escape applied before quoting: \\\\ -> \\ -> literal backslash in quotes.
    assert(*parse("app \"a\\\\\\\\b\"") == (V{"app", "a\\b"}));
    assert(*parse("env FOO=bar app") == (V{"env", "FOO=bar", "app"}));
    // Malformed / empty.
    assert(!parse("app \"unterminated"));
    assert(!parse(""));
    assert(!parse("   "));
    assert(!parse("%f %U"));
    // Shell metacharacters stay literal text.
    assert(*parse("app ; && $(x) `y` | z") == (V{"app", ";", "&&", "$(x)", "`y`", "|", "z"}));

    // launch_shell_command with harmless command 'true' should succeed
    {
        bool ok = Executor::launch_shell_command("true");
        assert(ok);
    }

    assert(Executor::launch_desktop_exec("/bin/true"));
    assert(!Executor::launch_desktop_exec("app \"unterminated"));
    assert(!Executor::launch_desktop_exec("/nonexistent/nexus-no-such-binary"));

    // launch_desktop_exec runs argv directly: a fake program records what it received.
    {
        const auto dir = std::filesystem::temp_directory_path() / ("nexus_test_desktop_" + std::to_string(getpid()));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const auto out = dir / "args.txt";
        for (const char* name : {"fakeapp", "x-terminal-emulator"}) {
            std::ofstream f(dir / name);
            f << "#!/bin/sh\nfor a in \"$@\"; do printf '[%s]' \"$a\"; done > '" << out.string() << "'\n";
            f.close();
            std::filesystem::permissions(dir / name, std::filesystem::perms::owner_all);
        }
        const char* cp = std::getenv("PATH");
        const std::string saved = cp ? cp : "";
        setenv("PATH", (dir.string() + ":" + saved).c_str(), 1);

        auto run = [&](const char* exec, bool term) {
            std::filesystem::remove(out);
            bool ok = Executor::launch_desktop_exec(exec, term);
            for (int i = 0; i < 250 && !std::filesystem::exists(out); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::string got;
            std::getline(std::ifstream(out), got);
            return std::make_pair(ok, got);
        };

        auto r = run("fakeapp ; touch PWNED && $(id) `id` | cat \"a b\" %f", false);
        assert(r.first);
        assert(r.second == "[;][touch][PWNED][&&][$(id)][`id`][|][cat][a b]");
        assert(!std::filesystem::exists("PWNED"));

        r = run("fakeapp \"x y\" z", true);
        assert(r.first);
        assert(r.second == "[-e][fakeapp][x y][z]");

        setenv("PATH", saved.c_str(), 1);
        std::filesystem::remove_all(dir);
    }

    // Launched programs lose the AppImage's variables only when this binary runs from inside $APPDIR.
    {
        const auto dir = std::filesystem::temp_directory_path() / ("nexus_test_env_" + std::to_string(getpid()));
        std::filesystem::remove_all(dir);
        std::filesystem::create_directories(dir);
        const auto out = dir / "env.txt";
        const auto exe_dir = std::filesystem::canonical("/proc/self/exe").parent_path();

        const auto script = dir / "dump.sh";
        {
            std::ofstream f(script);
            f << "#!/bin/sh\nenv > '" << out.string() << ".tmp' && mv '" << out.string() << ".tmp' '" << out.string()
              << "'\n";
        }
        std::filesystem::permissions(script, std::filesystem::perms::owner_all);

        // Runs the dump script directly or through the shell path and returns the env it saw.
        auto dump_env = [&](bool via_shell) {
            std::filesystem::remove(out);
            bool ok = false;
            if (via_shell) {
                ok = Executor::launch_shell_command("'" + script.string() + "'");
            } else {
                ok = Executor::launch_desktop_exec(script.string());
            }
            CHECK_EQ(ok, true);
            for (int i = 0; i < 250 && !std::filesystem::exists(out); ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
            std::ifstream in(out);
            return "\n" + std::string(std::istreambuf_iterator<char>(in), {});
        };
        auto has = [](const std::string& all, const char* name) {
            return all.find(std::string("\n") + name + "=") != std::string::npos;
        };

        setenv("KEEP_ME", "1", 1);
        setenv("APPIMAGE", "/tmp/x.AppImage", 1);
        setenv("ARGV0", "x", 1);
        setenv("OWD", "/tmp", 1);
        setenv("QT_QPA_PLATFORMTHEME", "gtk2", 1);

        // $APPDIR set but this binary is not inside it: nothing is removed.
        setenv("APPDIR", dir.c_str(), 1);
        for (const bool via_shell : {false, true}) {
            const std::string all = dump_env(via_shell);
            for (const char* name : {"APPDIR", "APPIMAGE", "ARGV0", "OWD", "QT_QPA_PLATFORMTHEME"}) {
                CHECK_EQ(has(all, name), true);
            }
        }

        // Running from inside $APPDIR: the bundle variables are gone, everything else stays.
        setenv("APPDIR", exe_dir.c_str(), 1);
        for (const bool via_shell : {false, true}) {
            const std::string all = dump_env(via_shell);
            for (const char* name : {"APPDIR", "APPIMAGE", "ARGV0", "OWD", "QT_QPA_PLATFORMTHEME"}) {
                CHECK_EQ(has(all, name), false);
            }
            CHECK_EQ(has(all, "KEEP_ME"), true);
        }

        for (const char* name : {"KEEP_ME", "APPDIR", "APPIMAGE", "ARGV0", "OWD", "QT_QPA_PLATFORMTHEME"}) {
            unsetenv(name);
        }
        std::filesystem::remove_all(dir);
    }

    // open_path_or_url must keep redirected descriptors open until xdg-open starts.
    {
        const auto fake_bin = std::filesystem::temp_directory_path() / "nexus_test_xdg_open";
        std::filesystem::remove_all(fake_bin);
        std::filesystem::create_directories(fake_bin);
        std::filesystem::create_symlink("/usr/bin/true", fake_bin / "xdg-open");

        const char* current_path = std::getenv("PATH");
        const std::string saved_path = current_path ? current_path : "";
        setenv("PATH", fake_bin.c_str(), 1);
        bool ok = Executor::open_path_or_url("https://example.invalid");
        if (current_path) {
            setenv("PATH", saved_path.c_str(), 1);
        } else {
            unsetenv("PATH");
        }

        std::filesystem::remove_all(fake_bin);
        assert(ok);

        // The finished opener must be reaped: wait until no child (live or zombie)
        // remains, so the check cannot pass just because it ran before the child exited.
        bool child = true;
        for (int i = 0; i < 100 && child; ++i) {
            child = has_child();
            if (child) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        assert(!child);
    }

    // spawn_with_pipes / ChildProcess
    {
        using ms = std::chrono::milliseconds;
        using RS = ChildProcess::ReadStatus;
        const auto dir = std::filesystem::temp_directory_path() / ("nexus_test_pipes_" + std::to_string(getpid()));
        std::filesystem::create_directories(dir);
        const std::atomic<bool> no_cancel{false};
        auto soon = [](int n) { return std::chrono::steady_clock::now() + std::chrono::milliseconds(n); };
        std::string line;

        // Round trip through cat; stderr lands in <cwd>/plugin.log; cwd is honoured.
        {
            auto p = Executor::spawn_with_pipes({"/bin/sh", "-c", "echo err >&2; pwd; exec cat"}, dir);
            assert(p.valid());
            assert(p.read_line(line, soon(2000), no_cancel) == RS::Line);
            assert(std::filesystem::equivalent(line, dir));
            assert(p.write_line("hello\n"));
            assert(p.read_line(line, soon(2000), no_cancel) == RS::Line && line == "hello");
            p.terminate(ms(0));
            assert(!p.valid());
            std::ifstream log(dir / "plugin.log");
            std::getline(log, line);
            assert(line == "err");
        }
        // Nothing to read: Timeout, promptly; a set cancel token wins.
        {
            auto p = Executor::spawn_with_pipes({"/bin/sh", "-c", "sleep 5"}, {});
            const auto t0 = std::chrono::steady_clock::now();
            assert(p.read_line(line, soon(60), no_cancel) == RS::Timeout);
            assert(std::chrono::steady_clock::now() - t0 < ms(150));
            const std::atomic<bool> cancelled{true};
            assert(p.read_line(line, soon(2000), cancelled) == RS::Cancelled);
        }
        // A partial line stays buffered until the rest arrives; two lines in one burst come out one by one.
        {
            auto p = Executor::spawn_with_pipes({"/bin/sh", "-c", "printf 'ab'; sleep 0.3; printf 'c\\nd\\n'"}, {});
            assert(p.read_line(line, soon(100), no_cancel) == RS::Timeout);
            assert(p.read_line(line, soon(2000), no_cancel) == RS::Line && line == "abc");
            assert(p.read_line(line, soon(2000), no_cancel) == RS::Line && line == "d");
            assert(p.read_line(line, soon(2000), no_cancel) == RS::Closed);
        }
        // Over-long line without a newline is refused instead of buffered forever.
        {
            auto p =
                Executor::spawn_with_pipes({"/bin/sh", "-c", "head -c 100000 /dev/zero | tr '\\0' x; sleep 5"}, {});
            assert(p.read_line(line, soon(3000), no_cancel, 1000) == RS::TooLong);
        }
        // Writing to a dead child fails (EPIPE) and must not kill us (SIGPIPE ignored);
        // a full pipe fails instead of blocking.
        {
            auto p = Executor::spawn_with_pipes({"/bin/sh", "-c", "exit 0"}, {});
            assert(p.read_line(line, soon(2000), no_cancel) == RS::Closed);
            bool ok = true;
            for (int i = 0; i < 50 && ok; ++i) {
                ok = p.write_line("x\n");
                std::this_thread::sleep_for(ms(5));
            }
            assert(!ok);
            auto q = Executor::spawn_with_pipes({"/bin/sh", "-c", "sleep 5"}, {});
            const std::string big(1 << 20, 'y');
            const auto t0 = std::chrono::steady_clock::now();
            assert(!q.write_line(big + "\n"));
            assert(std::chrono::steady_clock::now() - t0 < ms(500));  // O_NONBLOCK: no waiting for `sleep 5`
        }
        // SIGPIPE is ignored in Nexus now, but no child inherits that: plugins, apps and `>` commands.
        {
            auto sigpipe_ignored = [](const std::string& status_line) {  // "SigIgn:\t<hex mask>"
                assert(status_line.rfind("SigIgn:", 0) == 0);
                return (std::stoull(status_line.substr(7), nullptr, 16) >> (SIGPIPE - 1)) & 1;
            };
            auto p = Executor::spawn_with_pipes({"/bin/grep", "SigIgn", "/proc/self/status"}, {});
            assert(p.read_line(line, soon(2000), no_cancel) == RS::Line && !sigpipe_ignored(line));
            auto file_line = [&](const std::filesystem::path& f) {
                for (int i = 0; i < 250 && !std::filesystem::exists(f); ++i) std::this_thread::sleep_for(ms(20));
                std::this_thread::sleep_for(ms(50));  // let the writer finish the line
                std::string l;
                std::getline(std::ifstream(f), l);
                return l;
            };
            assert(Executor::launch_desktop_exec("/bin/sh -c \"grep SigIgn /proc/self/status > '" +
                                                 (dir / "app").string() + "'\""));
            assert(!sigpipe_ignored(file_line(dir / "app")));
            assert(Executor::launch_shell_command("grep SigIgn /proc/self/status > '" + (dir / "cmd").string() + "'"));
            assert(!sigpipe_ignored(file_line(dir / "cmd")));
        }
        // terminate() kills a child that ignores EOF, and the destructor reaps it.
        {
            auto p = Executor::spawn_with_pipes({"/bin/sh", "-c", "trap '' TERM; sleep 30"}, {});
            const auto t0 = std::chrono::steady_clock::now();
            p.terminate(ms(50));
            assert(std::chrono::steady_clock::now() - t0 < ms(1000));
        }
        assert(!Executor::spawn_with_pipes({"/nonexistent/nexus-plugin"}, {}).valid());
        assert(!has_child());
        std::filesystem::remove_all(dir);
    }

    // A shell command that fails is reported with its status; success is silent; the caller never waits for it.
    {
        struct Seen {
            std::mutex m;
            std::vector<std::string> reasons;
        };
        auto run = [](const char* command, Seen& seen, bool expect_report) {
            const bool started =
                Executor::launch_shell_command(command, [&seen](const std::string&, const std::string& reason) {
                    std::lock_guard<std::mutex> lock(seen.m);
                    seen.reasons.push_back(reason);
                });
            assert(started);
            for (int i = 0; i < 250; ++i) {
                {
                    std::lock_guard<std::mutex> lock(seen.m);
                    if (!seen.reasons.empty()) return;
                }
                if (!expect_report && i >= 10) return;  // a success has nothing to wait for
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        };
        Seen failed;
        run("false", failed, true);
        assert(failed.reasons == std::vector<std::string>{"exited with status 1"});
        Seen three;
        run("exit 3", three, true);
        assert(three.reasons == std::vector<std::string>{"exited with status 3"});
        Seen killed;
        run("kill -9 $$", killed, true);
        assert(killed.reasons == std::vector<std::string>{"killed by signal 9"});
        Seen ok;
        run("true", ok, false);
        assert(ok.reasons.empty());

        // Long-running command: launch returns at once, the report arrives after it ends.
        Seen slow;
        const auto t0 = std::chrono::steady_clock::now();
        run("sleep 0.3; exit 2", slow, true);
        assert(slow.reasons == std::vector<std::string>{"exited with status 2"});
        assert(std::chrono::steady_clock::now() - t0 >= std::chrono::milliseconds(250));
        assert(Executor::launch_shell_command("exit 1"));  // no callback is fine

        // The child's stdin, stdout and stderr all point at /dev/null (fd 3 carries the answer out).
        const auto stdio_file = std::filesystem::temp_directory_path() / ("nexus_stdio_" + std::to_string(getpid()));
        assert(Executor::launch_shell_command("exec 3>'" + stdio_file.string() +
                                              "'; for fd in 0 1 2; do readlink /proc/$$/fd/$fd | cat >&3; done"));
        std::string stdio_text;
        for (int i = 0; i < 250 && std::count(stdio_text.begin(), stdio_text.end(), '\n') < 3; ++i) {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            std::ifstream in(stdio_file);
            stdio_text.assign(std::istreambuf_iterator<char>(in), {});
        }
        assert(stdio_text == "/dev/null\n/dev/null\n/dev/null\n");
        std::filesystem::remove(stdio_file);
    }

    // run_capture: stdout and exit status, no shell, timeout kills a slow child.
    {
        std::string out;
        assert(Executor::run_capture({"echo", "hello world"}, out));
        assert(out == "hello world\n");
        assert(!Executor::run_capture({"false"}, out));
        assert(!Executor::run_capture({"/nonexistent/nexus-test-binary"}, out));
        assert(!Executor::run_capture({}, out));
        assert(Executor::run_capture({"echo", "a;echo b"}, out));
        assert(out == "a;echo b\n");  // no shell involved
        const auto t0 = std::chrono::steady_clock::now();
        assert(!Executor::run_capture({"sleep", "5"}, out, std::chrono::milliseconds(100)));
        assert(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    }

    // run_capture: output past the cap is a failure (never a truncated value), and the endless child is killed.
    {
        std::string out;
        const auto t0 = std::chrono::steady_clock::now();
        assert(!Executor::run_capture({"head", "-c", "70000", "/dev/zero"}, out));  // exits 0, but output was cut
        assert(!Executor::run_capture({"yes"}, out));
        assert(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    }

    // run_capture: a child that closes stdout but keeps running is not waited on past the timeout.
    {
        std::string out;
        const auto t0 = std::chrono::steady_clock::now();
        assert(!Executor::run_capture({"sh", "-c", "exec >&-; sleep 5"}, out, std::chrono::milliseconds(200)));
        assert(std::chrono::steady_clock::now() - t0 < std::chrono::seconds(3));
    }

    // run_capture: the child gets SIGPIPE back at its default even though Nexus ignores it.
    {
        const auto previous = signal(SIGPIPE, SIG_IGN);
        std::string out;
        assert(Executor::run_capture({"grep", "^SigIgn:", "/proc/self/status"}, out));
        const unsigned long ignored = std::stoul(out.substr(out.find(':') + 1), nullptr, 16);
        assert((ignored & (1UL << (SIGPIPE - 1))) == 0);
        signal(SIGPIPE, previous);
    }

    // Do not spawn a real GUI opener in CI; the regression above uses a fake.

    std::cout << "All Executor tests passed successfully!\n";
    return 0;
}
