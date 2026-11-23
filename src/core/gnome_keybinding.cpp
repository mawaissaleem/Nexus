#include "nexus/core/gnome_keybinding.hpp"
#include "nexus/core/executor.hpp"
#include "nexus/utils/hotkey_backend.hpp"

namespace nexus::core {

namespace {

const std::string kListSchema = "org.gnome.settings-daemon.plugins.media-keys";
const std::string kListKey = "custom-keybindings";
const std::string kEntrySchema = kListSchema + ".custom-keybinding";
const std::string kEntryPath = "/org/gnome/settings-daemon/plugins/media-keys/custom-keybindings/nexus/";

// The command runs through GNOME's shell-style parser, so a path with spaces needs quotes. Nothing escapes
// inside single quotes, so a quote in the path closes them, adds \' and reopens them.
std::string toggle_command(const std::string& binary_path) {
    if (binary_path.find_first_of(" \t'\"\\") == std::string::npos) return binary_path + " --toggle";

    std::string quoted = "'";
    for (const char c : binary_path) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    return quoted + "' --toggle";
}

}  // namespace

GnomeSetupResult setup_gnome_shortcut(const std::string& hotkey, const std::string& binary_path,
                                      const GsettingsFn& gsettings) {
    const auto accelerator = utils::to_accelerator(hotkey);
    if (!accelerator) return {false, "'" + hotkey + "' is not a valid shortcut."};

    // GNOME already uses Alt+Space for the window menu; don't silently take it over.
    if (*accelerator == "<Alt>space") {
        return {false, "Alt+Space is GNOME's own window-menu shortcut. Pick another shortcut and apply it first."};
    }

    const auto current = gsettings({"get", kListSchema, kListKey});
    if (!current) return {false, "Could not read GNOME's custom shortcuts (is gsettings installed?)."};

    const auto paths = utils::with_item(utils::parse_gsettings_list(*current), kEntryPath);
    const std::string entry = kEntrySchema + ":" + kEntryPath;
    const bool written = gsettings({"set", entry, "name", utils::gvariant_quote("Nexus")}) &&
                         gsettings({"set", entry, "command", utils::gvariant_quote(toggle_command(binary_path))}) &&
                         gsettings({"set", entry, "binding", utils::gvariant_quote(*accelerator)}) &&
                         gsettings({"set", kListSchema, kListKey, utils::format_gsettings_list(paths)});
    if (!written) return {false, "GNOME refused the change; your shortcuts were not fully updated."};

    return {true, "GNOME shortcut set: " + hotkey + " runs nexus --toggle."};
}

GsettingsFn system_gsettings() {
    return [](const std::vector<std::string>& args) -> std::optional<std::string> {
        std::vector<std::string> argv{"gsettings"};
        argv.insert(argv.end(), args.begin(), args.end());
        std::string out;
        if (!Executor::run_capture(argv, out)) return std::nullopt;
        return out;
    };
}

}  // namespace nexus::core
