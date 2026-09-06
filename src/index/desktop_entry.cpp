#include "nexus/index/desktop_entry.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>

namespace nexus::index {

namespace {

std::string trim(std::string_view s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) {
        ++start;
    }
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) {
        --end;
    }
    return std::string(s.substr(start, end - start));
}

std::vector<std::string> split_semicolon(std::string_view s) {
    std::vector<std::string> items;
    size_t start = 0;
    while (start < s.size()) {
        size_t end = s.find(';', start);
        if (end == std::string_view::npos) {
            end = s.size();
        }
        std::string item = trim(s.substr(start, end - start));
        if (!item.empty()) {
            items.push_back(std::move(item));
        }
        start = end + 1;
    }
    return items;
}

bool parse_bool(std::string_view s) {
    std::string lower = trim(s);
    for (char& c : lower) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return lower == "true" || lower == "1" || lower == "yes";
}

} // namespace

std::optional<DesktopEntry> DesktopEntry::parse(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::nullopt;
    }

    DesktopEntry entry;
    entry.desktop_file = path.string();
    entry.id = path.stem().string();

    std::string line;
    bool in_desktop_entry_section = false;
    std::string type_field;

    while (std::getline(file, line)) {
        std::string trimmed = trim(line);
        if (trimmed.empty() || trimmed.front() == '#') {
            continue;
        }

        if (trimmed.front() == '[' && trimmed.back() == ']') {
            in_desktop_entry_section = (trimmed == "[Desktop Entry]");
            continue;
        }

        if (!in_desktop_entry_section) {
            continue;
        }

        size_t eq = trimmed.find('=');
        if (eq == std::string::npos) {
            continue;
        }

        std::string key = trim(trimmed.substr(0, eq));
        std::string value = trim(trimmed.substr(eq + 1));

        // Skip localized keys like Name[de]=...
        if (key.find('[') != std::string::npos) {
            continue;
        }

        if (key == "Type") {
            type_field = value;
        } else if (key == "Name") {
            entry.name = value;
        } else if (key == "GenericName") {
            entry.generic_name = value;
        } else if (key == "Comment") {
            entry.comment = value;
        } else if (key == "Exec") {
            entry.exec = value;
        } else if (key == "Icon") {
            entry.icon = value;
        } else if (key == "Categories") {
            entry.categories = split_semicolon(value);
        } else if (key == "Keywords") {
            entry.keywords = split_semicolon(value);
        } else if (key == "NoDisplay") {
            entry.no_display = parse_bool(value);
        } else if (key == "Hidden") {
            entry.hidden = parse_bool(value);
        } else if (key == "Terminal") {
            entry.terminal = parse_bool(value);
        }
    }

    if (type_field.empty()) {
        type_field = "Application";
    }

    if (type_field != "Application" || entry.name.empty() || entry.exec.empty()) {
        return std::nullopt;
    }

    return entry;
}

} // namespace nexus::index

