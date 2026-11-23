#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace nexus::providers {

// True when a shell command line looks destructive (rm -rf, dd, mkfs, shutdown, ...), so the user is asked first.
// A safety net for typos, not a security boundary: the user can run anything after `>` anyway.
[[nodiscard]] bool is_dangerous_command(std::string_view command_line);

// Same check for an argv that is launched directly, without a shell: no operators or redirections, one command.
[[nodiscard]] bool is_dangerous_argv(const std::vector<std::string>& argv);

}  // namespace nexus::providers
