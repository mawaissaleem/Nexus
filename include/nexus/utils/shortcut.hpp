#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace nexus::utils {

// A parsed "Ctrl+Alt+Space" style shortcut. `key` is the last token as typed
// (e.g. "Space", "F1", "a"); the UI layer maps it to a platform keysym.
struct Shortcut {
    bool ctrl{false};
    bool alt{false};
    bool shift{false};
    bool super{false};
    std::string key;
};

// Modifier names are case-insensitive (Ctrl/Control, Alt, Shift, Super/Meta/Win).
// Returns nullopt unless there is at least one modifier and exactly one non-modifier key last.
std::optional<Shortcut> parse_shortcut(std::string_view text);

// True when XDG_SESSION_TYPE is "wayland" or WAYLAND_DISPLAY is non-empty.
// Global key grabs don't work there, so the user must bind `nexus --toggle` in the compositor.
bool is_wayland_session();

} // namespace nexus::utils
