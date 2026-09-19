#include "nexus/index/desktop_entry.hpp"
#include "nexus/core/executor.hpp"
#include <iostream>
#include <fstream>
#include <filesystem>
#include <cassert>

int main() {
    using namespace nexus::index;
    using namespace nexus::core;

    std::filesystem::path temp_dir = std::filesystem::temp_directory_path() / "nexus_test_desktop";
    std::filesystem::create_directories(temp_dir);
    std::filesystem::path test_desktop_file = temp_dir / "test_app.desktop";

    {
        std::ofstream out(test_desktop_file);
        out << "[Desktop Entry]\n"
            << "Type=Application\n"
            << "Name=Test Editor\n"
            << "GenericName=Text Editor\n"
            << "Comment=Edit text files\n"
            << "Exec=test-editor %F --new-window\n"
            << "Icon=test-editor\n"
            << "Categories=Development;TextEditor;\n"
            << "Keywords=code;text;editor;\n"
            << "Terminal=false\n"
            << "NoDisplay=false\n";
    }

    auto parsed = DesktopEntry::parse(test_desktop_file);
    assert(parsed.has_value());
    assert(parsed->name == "Test Editor");
    assert(parsed->generic_name == "Text Editor");
    assert(parsed->comment == "Edit text files");
    assert(parsed->exec == "test-editor %F --new-window");
    assert(parsed->icon == "test-editor");
    assert(!parsed->terminal);
    assert(!parsed->no_display);
    assert(parsed->categories.size() == 2);
    assert(parsed->categories[0] == "Development");
    assert(parsed->keywords.size() == 3);
    assert(parsed->keywords[0] == "code");
    assert(parsed->is_valid_application());

    // Test exec string cleaning
    std::string cleaned = Executor::clean_desktop_exec(parsed->exec);
    assert(cleaned == "test-editor  --new-window");

    // Regression: a malicious filename must not be interpreted by a shell.
    const auto temp_root = std::filesystem::temp_directory_path() / "nexus_test_injection";
    std::filesystem::create_directories(temp_root);
    const auto marker_path = std::filesystem::temp_directory_path() / "nexus_shell_injection_marker";
    std::filesystem::remove(marker_path);

    const auto injection_name = std::string("x\"; touch ") + marker_path.string() + ";\"";
    const auto malicious_path = temp_root / injection_name;
    {
        std::ofstream out(malicious_path);
        out << "payload\n";
    }

    Executor::open_path_or_url(malicious_path.string());
    assert(!std::filesystem::exists(marker_path));

    std::filesystem::remove_all(temp_root);
    std::filesystem::remove(marker_path);

    // Clean up
    std::filesystem::remove_all(temp_dir);

    std::cout << "All DesktopEntry tests passed successfully!\n";
    return 0;
}

