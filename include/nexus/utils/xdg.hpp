#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace nexus::utils {

// $HOME if it is absolute, else empty: a relative HOME counts as unset, so nothing resolves against the cwd.
[[nodiscard]] std::filesystem::path home_dir();

// Per-user XDG base dir: $<xdg_var> if it is absolute, else $HOME/<home_subdir> if HOME is absolute, else empty.
// Relative values count as unset, as the XDG spec says, so nothing resolves against the current directory.
[[nodiscard]] std::filesystem::path xdg_user_dir(const char* xdg_var, const std::filesystem::path& home_subdir);

// Expands a leading `~` or `~/` to home_dir(). Other paths come back unchanged. Nullopt when `~` needs a home
// and there is no usable (absolute) one, so a caller never ends up with a path relative to the cwd.
[[nodiscard]] std::optional<std::string> expand_home(std::string_view path);

// $APPDIR (canonical) when this very binary runs from inside it, else empty. Programs launched from an AppImage
// inherit $APPDIR, and once the image unmounts another user could recreate that path, so the variable alone
// proves nothing.
[[nodiscard]] std::filesystem::path appimage_root();

}  // namespace nexus::utils
