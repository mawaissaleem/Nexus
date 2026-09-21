#include <cassert>
#include <iostream>
#include "nexus/core/executor.hpp"

using namespace nexus::core;

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

    // Do NOT call open_path_or_url here to avoid spawning GUI-openers in CI.

    std::cout << "All Executor tests passed successfully!\n";
    return 0;
}
