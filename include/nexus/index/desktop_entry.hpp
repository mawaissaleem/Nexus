#pragma once

#include <string>
#include <vector>
#include <optional>
#include <filesystem>

namespace nexus::index {

struct DesktopEntry {
    std::string id;            // Unique identifier, typically filename without .desktop
    std::string name;          // Application display name
    std::string generic_name;  // Generic name, e.g. "Text Editor"
    std::string comment;       // Description/comment
    std::string exec;          // Command line execution string
    std::string icon;          // Icon name or absolute path
    std::string desktop_file;  // Absolute path to the .desktop file
    std::vector<std::string> categories;
    std::vector<std::string> keywords;

    bool no_display{false};
    bool hidden{false};
    bool terminal{false};

    // Parse a .desktop file from filesystem
    static std::optional<DesktopEntry> parse(const std::filesystem::path& path);

    // Helper: is this entry valid for launching and display?
    [[nodiscard]] bool is_valid_application() const noexcept {
        return !name.empty() && !exec.empty() && !no_display && !hidden;
    }
};

} // namespace nexus::index

