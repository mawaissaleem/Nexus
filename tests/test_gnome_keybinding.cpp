#include "nexus/core/gnome_keybinding.hpp"
#include "nexus/utils/hotkey_backend.hpp"
#include <cassert>
#include <iostream>
#include <map>

using namespace nexus::core;

// Fake gsettings: records every `set`, serves `get` from a fixed list. No real gsettings is touched.
struct Fake {
    explicit Fake(std::string l, bool get_fails = false, bool set_fails = false)
        : list(std::move(l)), fail_get(get_fails), fail_set(set_fails) {}

    std::string list;
    bool fail_get;
    bool fail_set;
    std::vector<std::vector<std::string>> sets;

    GsettingsFn fn() {
        return [this](const std::vector<std::string>& args) -> std::optional<std::string> {
            if (args[0] == "get") {
                if (fail_get) return std::nullopt;
                return list;
            }
            if (fail_set) return std::nullopt;
            sets.push_back(args);
            return std::string();
        };
    }
};

static const char* kNexusPath = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/nexus/";

int main() {
    // Empty list: entry fields written, then the list holds just the Nexus path.
    {
        Fake f("@as []\n");
        const auto r = setup_gnome_shortcut("Ctrl+Alt+Space", "/usr/bin/nexus", f.fn());
        assert(r.ok);
        assert(f.sets.size() == 4);
        assert(f.sets[0][1].find(kNexusPath) != std::string::npos);
        assert(f.sets[1][3] == "'/usr/bin/nexus --toggle'");
        assert(f.sets[2][3] == "'<Control><Alt>space'");
        assert(f.sets[3][2] == "custom-keybindings");  // the list is written last
        assert(f.sets[3][3] == std::string("['") + kNexusPath + "']");
    }

    // Existing custom keybindings are kept and the Nexus one is appended.
    {
        Fake f("['/other/custom0/']\n");
        assert(setup_gnome_shortcut("Ctrl+Alt+N", "/usr/bin/nexus", f.fn()).ok);
        assert(f.sets.back()[3] == std::string("['/other/custom0/', '") + kNexusPath + "']");
    }

    // Re-clicking: the same entry is updated, the list is not extended.
    {
        Fake f(std::string("['/other/custom0/', '") + kNexusPath + "']\n");
        assert(setup_gnome_shortcut("Ctrl+Alt+M", "/usr/bin/nexus", f.fn()).ok);
        assert(f.sets.back()[3] == f.list.substr(0, f.list.size() - 1));
        assert(f.sets[2][3] == "'<Control><Alt>m'");
    }

    // A binary path with a space is quoted for GNOME's command parser.
    {
        Fake f("@as []");
        assert(setup_gnome_shortcut("Ctrl+Alt+Space", "/opt/my apps/nexus", f.fn()).ok);
        assert(f.sets[1][3] == "'\\'/opt/my apps/nexus\\' --toggle'");
    }

    // A quote in the path can't end the single-quoted word early: it becomes '\'' (close, escaped quote, reopen).
    {
        Fake f("@as []");
        assert(setup_gnome_shortcut("Ctrl+Alt+Space", "/home/u/Bob's tools/nexus", f.fn()).ok);
        const auto command = nexus::utils::parse_gsettings_list(f.sets[1][3]);  // undo the GVariant quoting
        assert(command.size() == 1);
        assert(command[0] == "'/home/u/Bob'\\''s tools/nexus' --toggle");
    }

    // GNOME's own Alt+Space is refused without touching anything.
    {
        Fake f("@as []");
        const auto r = setup_gnome_shortcut("Alt+Space", "/usr/bin/nexus", f.fn());
        assert(!r.ok && r.message.find("window-menu") != std::string::npos);
        assert(f.sets.empty());
    }

    // Invalid shortcut, unreadable gsettings, refused write: reported, not ok.
    {
        Fake f("@as []");
        assert(!setup_gnome_shortcut("Space", "/usr/bin/nexus", f.fn()).ok);
        assert(f.sets.empty());
    }
    {
        Fake f("", true);
        assert(!setup_gnome_shortcut("Ctrl+Alt+Space", "/usr/bin/nexus", f.fn()).ok);
        assert(f.sets.empty());
    }
    {
        Fake f("@as []", false, true);
        assert(!setup_gnome_shortcut("Ctrl+Alt+Space", "/usr/bin/nexus", f.fn()).ok);
    }

    std::cout << "All GNOME keybinding tests passed successfully!\n";
    return 0;
}
