#pragma once

#include <string_view>

namespace nexus::providers {

// True when a shell command line looks destructive (rm -rf, dd, mkfs, shutdown, ...), so the user is asked first.
// A safety net for typos, not a security boundary: the user can run anything after `>` anyway.
[[nodiscard]] bool is_dangerous_command(std::string_view command_line);

}  // namespace nexus::providers
