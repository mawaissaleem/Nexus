#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace nexus::core {

// Runs `gsettings <args>` and returns its stdout, or nullopt if it failed. Injected so tests never touch gsettings.
using GsettingsFn = std::function<std::optional<std::string>(const std::vector<std::string>& args)>;

struct GnomeSetupResult {
    bool ok{false};
    std::string message;  // shown to the user either way
};

// Adds (or updates in place) the one Nexus entry under GNOME's custom keybindings so that `hotkey` runs
// `<binary_path> --toggle`. Other custom keybindings are kept. Refuses GNOME's own Alt+Space window-menu key.
GnomeSetupResult setup_gnome_shortcut(const std::string& hotkey, const std::string& binary_path,
                                      const GsettingsFn& gsettings);

// The real thing: Executor::run_capture on gsettings.
GsettingsFn system_gsettings();

}  // namespace nexus::core
