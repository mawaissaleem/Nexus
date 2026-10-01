#include "nexus/utils/shortcut.hpp"
#include <cassert>
#include <cstdlib>
#include <iostream>
#include <string>

int main() {
    using nexus::utils::parse_shortcut;

    auto a = parse_shortcut("Alt+Space");
    assert(a && a->alt && !a->ctrl && !a->shift && !a->super && a->key == "Space");

    auto b = parse_shortcut(" ctrl + SHIFT + k ");
    assert(b && b->ctrl && b->shift && !b->alt && b->key == "k");

    auto c = parse_shortcut("Control+Meta+F1");
    assert(c && c->ctrl && c->super && c->key == "F1");

    assert(!parse_shortcut(""));
    assert(!parse_shortcut("Space"));        // no modifier
    assert(!parse_shortcut("Alt+"));         // missing key
    assert(!parse_shortcut("+Space"));
    assert(!parse_shortcut("Alt++Space"));
    assert(!parse_shortcut("Alt+Ctrl"));     // modifier as key
    assert(!parse_shortcut("Foo+Space"));    // unknown modifier

    // Wayland detection; save and restore the real environment.
    const char* t = std::getenv("XDG_SESSION_TYPE");
    const char* d = std::getenv("WAYLAND_DISPLAY");
    const std::string old_t = t ? t : "", old_d = d ? d : "";
    unsetenv("XDG_SESSION_TYPE"); unsetenv("WAYLAND_DISPLAY");
    assert(!nexus::utils::is_wayland_session());
    setenv("XDG_SESSION_TYPE", "x11", 1);
    assert(!nexus::utils::is_wayland_session());
    setenv("WAYLAND_DISPLAY", "", 1);
    assert(!nexus::utils::is_wayland_session());   // empty display does not count
    setenv("XDG_SESSION_TYPE", "wayland", 1);
    assert(nexus::utils::is_wayland_session());
    unsetenv("XDG_SESSION_TYPE");
    setenv("WAYLAND_DISPLAY", "wayland-0", 1);
    assert(nexus::utils::is_wayland_session());
    if (t) setenv("XDG_SESSION_TYPE", old_t.c_str(), 1); else unsetenv("XDG_SESSION_TYPE");
    if (d) setenv("WAYLAND_DISPLAY", old_d.c_str(), 1); else unsetenv("WAYLAND_DISPLAY");

    std::cout << "All Shortcut tests passed successfully!\n";
    return 0;
}
