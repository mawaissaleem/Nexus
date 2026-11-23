#include "nexus/utils/hotkey_backend.hpp"
#include <cassert>
#include <iostream>

using namespace nexus::utils;

int main() {
    // Backend selection
    assert(select_hotkey_backend(false, false, "GNOME") == HotkeyBackend::X11Grab);
    assert(select_hotkey_backend(false, true, "KDE") == HotkeyBackend::X11Grab);
    assert(select_hotkey_backend(true, true, "KDE") == HotkeyBackend::Portal);
    assert(select_hotkey_backend(true, true, "GNOME") == HotkeyBackend::Portal);
    assert(select_hotkey_backend(true, false, "ubuntu:GNOME") == HotkeyBackend::GnomeCustom);
    assert(select_hotkey_backend(true, false, "GNOME") == HotkeyBackend::GnomeCustom);
    assert(select_hotkey_backend(true, false, "KDE") == HotkeyBackend::None);
    assert(select_hotkey_backend(true, false, "GNOME-Classic") == HotkeyBackend::None);  // exact token only
    assert(select_hotkey_backend(true, false, "") == HotkeyBackend::None);

    // Hotkey string -> accelerator
    assert(to_accelerator("Ctrl+Alt+Space") == "<Control><Alt>space");
    assert(to_accelerator("alt+space") == "<Alt>space");
    assert(to_accelerator("Ctrl+Shift+K") == "<Control><Shift>k");
    assert(to_accelerator("Super+F1") == "<Super>F1");
    assert(to_accelerator("ctrl+return") == "<Control>Return");
    assert(to_accelerator("Ctrl+BackSpace") == "<Control>BackSpace");  // capitals typed by the user are kept
    assert(to_accelerator("Alt+Page_Up") == "<Alt>Page_Up");
    assert(to_accelerator("Ctrl+SPACE") == "<Control>space");
    assert(!to_accelerator("Space"));  // no modifier
    assert(!to_accelerator(""));

    // Hotkey string -> portal trigger (XDG shortcuts syntax, not the GTK one)
    assert(to_portal_trigger("Ctrl+Alt+Space") == "CTRL+ALT+space");
    assert(to_portal_trigger("Super+F1") == "LOGO+F1");
    assert(to_portal_trigger("Ctrl+Shift+K") == "CTRL+SHIFT+k");
    assert(to_portal_trigger("alt+return") == "ALT+Return");
    assert(!to_portal_trigger("Space"));

    // gsettings list parsing
    assert(parse_gsettings_list("@as []\n").empty());
    assert(parse_gsettings_list("[]").empty());
    assert((parse_gsettings_list("['/a/']\n") == std::vector<std::string>{"/a/"}));
    assert((parse_gsettings_list("['/a/', '/b/c/']") == std::vector<std::string>{"/a/", "/b/c/"}));
    assert((parse_gsettings_list("['it\\'s']") == std::vector<std::string>{"it's"}));

    // Formatting round-trips and an empty list uses the typed form gsettings needs
    assert(format_gsettings_list({}) == "@as []");
    assert(format_gsettings_list({"/a/", "/b/"}) == "['/a/', '/b/']");
    assert(parse_gsettings_list(format_gsettings_list({"it's"})) == std::vector<std::string>{"it's"});

    // Adding an entry: appended once, existing kept, never duplicated
    assert((with_item({}, "/n/") == std::vector<std::string>{"/n/"}));
    assert((with_item({"/a/"}, "/n/") == std::vector<std::string>{"/a/", "/n/"}));
    assert((with_item({"/n/", "/a/"}, "/n/") == std::vector<std::string>{"/n/", "/a/"}));

    assert(gvariant_quote("a b") == "'a b'");
    assert(gvariant_quote("it's") == "'it\\'s'");

    std::cout << "All hotkey backend tests passed successfully!\n";
    return 0;
}
