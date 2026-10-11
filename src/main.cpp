#include "nexus/app/cli.hpp"
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/ui/gui_app.hpp"
#include "nexus/utils/logger.hpp"
#include <QApplication>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    nexus::utils::Logger::instance().set_level(nexus::utils::LogLevel::Info);

    // Created once; every provider, window and dialog holds a reference, so they must outlive those.
    nexus::core::ConfigManager config(nexus::core::ConfigManager::resolve_config_path());
    nexus::core::AliasManager aliases(nexus::core::AliasManager::resolve_aliases_path());

    const std::vector<std::string> args(argv + 1, argv + argc);

    // CLI subcommands run before any QApplication exists, so they work without a display.
    if (const auto rc = nexus::app::run_cli(args, config, aliases, std::cout, std::cerr)) return *rc;

    QApplication app(argc, argv);
    app.setApplicationName("Nexus");
    app.setApplicationDisplayName("Nexus Launcher");
    app.setQuitOnLastWindowClosed(false);

    return nexus::ui::run_gui(app, args, config, aliases);
}
