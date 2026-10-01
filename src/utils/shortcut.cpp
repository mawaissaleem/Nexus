#include "nexus/utils/shortcut.hpp"
#include "nexus/utils/fuzzy.hpp"
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace nexus::utils {

std::optional<Shortcut> parse_shortcut(std::string_view text) {
    std::vector<std::string> tokens;
    size_t pos = 0;
    while (true) {
        size_t plus = text.find('+', pos);
        std::string_view tok = text.substr(pos, plus == std::string_view::npos ? plus : plus - pos);
        while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.front()))) tok.remove_prefix(1);
        while (!tok.empty() && std::isspace(static_cast<unsigned char>(tok.back()))) tok.remove_suffix(1);
        if (tok.empty()) return std::nullopt;
        tokens.emplace_back(tok);
        if (plus == std::string_view::npos) break;
        pos = plus + 1;
    }

    Shortcut sc;
    for (size_t i = 0; i + 1 < tokens.size(); ++i) {
        const std::string& t = tokens[i];
        if (FuzzyMatcher::iequals(t, "ctrl") || FuzzyMatcher::iequals(t, "control")) sc.ctrl = true;
        else if (FuzzyMatcher::iequals(t, "alt")) sc.alt = true;
        else if (FuzzyMatcher::iequals(t, "shift")) sc.shift = true;
        else if (FuzzyMatcher::iequals(t, "super") || FuzzyMatcher::iequals(t, "meta") || FuzzyMatcher::iequals(t, "win")) sc.super = true;
        else return std::nullopt;
    }
    if (!(sc.ctrl || sc.alt || sc.shift || sc.super)) return std::nullopt;

    sc.key = tokens.back();
    for (const char* mod : {"ctrl", "control", "alt", "shift", "super", "meta", "win"}) {
        if (FuzzyMatcher::iequals(sc.key, mod)) return std::nullopt;
    }
    return sc;
}

bool is_wayland_session() {
    const char* type = std::getenv("XDG_SESSION_TYPE");
    const char* display = std::getenv("WAYLAND_DISPLAY");
    return (type && std::strcmp(type, "wayland") == 0) || (display && *display);
}

} // namespace nexus::utils
