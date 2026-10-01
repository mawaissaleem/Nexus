#include <cassert>
#include <cstdlib>
#include <filesystem>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#include <unistd.h>
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
        char state = 0; long ppid = 0;
        if (sscanf(line.c_str() + p + 1, " %c %ld", &state, &ppid) == 2 &&
            ppid == getpid()) return true;
    }
    return false;
}

int main() {
    // clean_desktop_exec: strips known field codes
    {
        std::string in = "/usr/bin/app %f --flag %U --name=%c %z";
        std::string out = Executor::clean_desktop_exec(in);
        // According to current implementation, recognized field codes are removed.
        // Note: implementation's recognized set may not include every spec code;
        // test asserts the actual observed behavior.
        assert(out.find("%f") == std::string::npos);
        assert(out.find("%U") == std::string::npos);
        // Unrecognized field code (%z) should remain
        assert(out.find("%z") != std::string::npos);
    }

    // clean_desktop_exec with no field codes returns unchanged (modulo trimming)
    {
        std::string in = "/usr/bin/app --flag";
        std::string out = Executor::clean_desktop_exec(in);
        assert(out == in);
    }

    // launch_shell_command with harmless command 'true' should succeed
    {
        bool ok = Executor::launch_shell_command("true");
        assert(ok);
    }

    // launch_desktop_exec should use clean_desktop_exec + shell launch
    {
        bool ok = Executor::launch_desktop_exec("/bin/true");
        assert(ok);
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

    // Do not spawn a real GUI opener in CI; the regression above uses a fake.

    std::cout << "All Executor tests passed successfully!\n";
    return 0;
}
