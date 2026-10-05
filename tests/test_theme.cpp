#include "nexus/utils/theme.hpp"
#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

int main() {
    using namespace nexus::utils;

    // Name parsing: exact lowercase names only
    assert(parse_theme_mode("dark") == ThemeMode::Dark);
    assert(parse_theme_mode("light") == ThemeMode::Light);
    assert(parse_theme_mode("system") == ThemeMode::System);
    assert(!parse_theme_mode("Dark"));
    assert(!parse_theme_mode(""));
    assert(!parse_theme_mode("solarized"));

    // Names round-trip
    for (auto m : {ThemeMode::Dark, ThemeMode::Light, ThemeMode::System}) {
        assert(parse_theme_mode(theme_mode_name(m)) == m);
    }

    // Resolution: only System looks at the desktop
    assert(resolve_dark(ThemeMode::Dark, false));
    assert(resolve_dark(ThemeMode::Dark, true));
    assert(!resolve_dark(ThemeMode::Light, true));
    assert(!resolve_dark(ThemeMode::Light, false));
    assert(resolve_dark(ThemeMode::System, true));
    assert(!resolve_dark(ThemeMode::System, false));

    // Guard: colours live in src/ui/theme.cpp only. A hex colour, rgba() or QColor(r, g, b) anywhere else in
    // src/ui means a widget will ignore the chosen theme.
    namespace fs = std::filesystem;
    const std::regex colour(R"(#[0-9a-fA-F]{6}\b|rgba?\(|QColor\(\s*[0-9])");
    int scanned = 0;
    for (const auto& entry : fs::directory_iterator(fs::path(NEXUS_SOURCE_DIR) / "src" / "ui")) {
        if (entry.path().extension() != ".cpp" || entry.path().filename() == "theme.cpp") continue;
        std::ifstream in(entry.path());
        std::stringstream text;
        text << in.rdbuf();
        std::smatch m;
        const std::string body = text.str();
        if (std::regex_search(body, m, colour)) {
            std::cerr << entry.path() << " contains colour literal: " << m.str() << "\n";
            assert(false);
        }
        ++scanned;
    }
    assert(scanned >= 4);  // the directory was really found

    std::cout << "All theme tests passed successfully!\n";
    return 0;
}
