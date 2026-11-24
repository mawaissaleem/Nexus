#pragma once

#include <filesystem>

namespace nexus::utils {

// $HOME if it is absolute, else empty: a relative HOME counts as unset, so nothing resolves against the cwd.
[[nodiscard]] std::filesystem::path home_dir();

// Per-user XDG base dir: $<xdg_var> if it is absolute, else $HOME/<home_subdir> if HOME is absolute, else empty.
// Relative values count as unset, as the XDG spec says, so nothing resolves against the current directory.
[[nodiscard]] std::filesystem::path xdg_user_dir(const char* xdg_var, const std::filesystem::path& home_subdir);

}  // namespace nexus::utils
