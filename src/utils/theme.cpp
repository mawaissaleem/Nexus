#include "nexus/utils/theme.hpp"

namespace nexus::utils {

std::optional<ThemeMode> parse_theme_mode(std::string_view name) {
    if (name == "dark") return ThemeMode::Dark;
    if (name == "light") return ThemeMode::Light;
    if (name == "system") return ThemeMode::System;
    return std::nullopt;
}

const char* theme_mode_name(ThemeMode mode) {
    switch (mode) {
        case ThemeMode::Light: return "light";
        case ThemeMode::System: return "system";
        case ThemeMode::Dark: break;
    }
    return "dark";
}

bool resolve_dark(ThemeMode mode, bool system_is_dark) {
    return mode == ThemeMode::System ? system_is_dark : mode == ThemeMode::Dark;
}

} // namespace nexus::utils
