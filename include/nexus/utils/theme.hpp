#pragma once

#include <optional>
#include <string_view>

namespace nexus::utils {

enum class ThemeMode { Dark, Light, System };

// Exact lowercase names only ("dark", "light", "system"); anything else is nullopt.
std::optional<ThemeMode> parse_theme_mode(std::string_view name);
const char* theme_mode_name(ThemeMode mode);

// True when the dark palette should be used. `system_is_dark` only matters for ThemeMode::System.
bool resolve_dark(ThemeMode mode, bool system_is_dark);

} // namespace nexus::utils
