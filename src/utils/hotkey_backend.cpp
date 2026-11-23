#include "nexus/utils/hotkey_backend.hpp"
#include "nexus/utils/shortcut.hpp"
#include <algorithm>
#include <cctype>

namespace nexus::utils {

namespace {

bool desktop_has(std::string_view desktop, std::string_view name) {
    size_t pos = 0;
    while (pos <= desktop.size()) {
        size_t colon = desktop.find(':', pos);
        if (colon == std::string_view::npos) colon = desktop.size();
        if (desktop.substr(pos, colon - pos) == name) return true;
        pos = colon + 1;
    }
    return false;
}

// Keysym names: "space" and single letters are lowercase, a name typed with capitals is kept ("BackSpace",
// "Page_Up"), any other is capitalised ("return" -> "Return").
std::string keysym_name(const std::string& key) {
    std::string lower = key;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
    if (lower.size() == 1 || lower == "space") return lower;
    if (lower != key) return key;
    lower[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(lower[0])));
    return lower;
}

}  // namespace

HotkeyBackend select_hotkey_backend(bool wayland, bool portal_present, std::string_view desktop) {
    if (!wayland) return HotkeyBackend::X11Grab;
    if (portal_present) return HotkeyBackend::Portal;
    if (desktop_has(desktop, "GNOME")) return HotkeyBackend::GnomeCustom;
    return HotkeyBackend::None;
}

std::optional<std::string> to_accelerator(std::string_view shortcut) {
    const auto parsed = parse_shortcut(shortcut);
    if (!parsed) return std::nullopt;

    std::string out;
    if (parsed->ctrl) out += "<Control>";
    if (parsed->alt) out += "<Alt>";
    if (parsed->shift) out += "<Shift>";
    if (parsed->super) out += "<Super>";
    return out + keysym_name(parsed->key);
}

std::optional<std::string> to_portal_trigger(std::string_view shortcut) {
    const auto parsed = parse_shortcut(shortcut);
    if (!parsed) return std::nullopt;

    std::string out;
    if (parsed->ctrl) out += "CTRL+";
    if (parsed->alt) out += "ALT+";
    if (parsed->shift) out += "SHIFT+";
    if (parsed->super) out += "LOGO+";
    return out + keysym_name(parsed->key);
}

std::vector<std::string> parse_gsettings_list(std::string_view text) {
    std::vector<std::string> items;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '\'') {
            ++i;
            continue;
        }
        std::string item;
        for (++i; i < text.size() && text[i] != '\''; ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) ++i;
            item += text[i];
        }
        ++i;  // closing quote
        items.push_back(item);
    }
    return items;
}

std::string gvariant_quote(std::string_view text) {
    std::string out = "'";
    for (const char c : text) {
        if (c == '\'' || c == '\\') out += '\\';
        out += c;
    }
    return out + "'";
}

std::string format_gsettings_list(const std::vector<std::string>& items) {
    if (items.empty()) return "@as []";

    std::string out = "[";
    for (size_t i = 0; i < items.size(); ++i) {
        if (i > 0) out += ", ";
        out += gvariant_quote(items[i]);
    }
    return out + "]";
}

std::vector<std::string> with_item(std::vector<std::string> items, const std::string& item) {
    if (std::find(items.begin(), items.end(), item) == items.end()) items.push_back(item);
    return items;
}

}  // namespace nexus::utils
