#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace nexus::utils {

// How the global hotkey is delivered in this session.
enum class HotkeyBackend {
    X11Grab,      // XGrabKey on the root window
    Portal,       // org.freedesktop.portal.GlobalShortcuts
    GnomeCustom,  // no portal: the user can add a GNOME custom keybinding that runs `nexus --toggle`
    None,         // Wayland with neither; the user binds `nexus --toggle` in the compositor
};

// `desktop` is XDG_CURRENT_DESKTOP, a colon-separated list such as "ubuntu:GNOME".
HotkeyBackend select_hotkey_backend(bool wayland, bool portal_present, std::string_view desktop);

// "Ctrl+Alt+Space" -> "<Control><Alt>space" (GTK accelerator syntax, for gsettings). nullopt if it does not parse.
std::optional<std::string> to_accelerator(std::string_view shortcut);

// "Ctrl+Alt+Space" -> "CTRL+ALT+space" (XDG shortcuts syntax, for the portal). nullopt if it does not parse.
std::optional<std::string> to_portal_trigger(std::string_view shortcut);

// gsettings prints a string list as "@as []" or "['/a/', '/b/']".
std::vector<std::string> parse_gsettings_list(std::string_view text);
std::string format_gsettings_list(const std::vector<std::string>& items);

// `items` plus `item`, unless it is already there (order kept, no duplicates).
std::vector<std::string> with_item(std::vector<std::string> items, const std::string& item);

// A GVariant text string literal: 'it\'s'.
std::string gvariant_quote(std::string_view text);

}  // namespace nexus::utils
