#include "nexus/app/cli.hpp"
#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/ui/gui_app.hpp"
#include "nexus/ui/ipc.hpp"
#include "nexus/utils/logger.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    nexus::utils::Logger::instance().set_level(nexus::utils::LogLevel::Info);

    const std::vector<std::string> args(argv + 1, argv + argc);
    // Before the managers exist: resolving their paths creates the config dir and loading may rewrite files.
    if (const auto rc = nexus::app::run_info_cli(args, std::cout, std::cerr)) return *rc;

    // Created once; every provider, window and dialog holds a reference, so they must outlive those.
    nexus::core::ConfigManager config(nexus::core::ConfigManager::resolve_config_path());
    nexus::core::AliasManager aliases(nexus::core::AliasManager::resolve_aliases_path());

    // CLI subcommands run before any QApplication exists, so they work without a display.
    // The CLI stays Qt-free; this is how `history clear` reaches a running launcher.
    const auto clear_running_clipboard = [&]() {
        QCoreApplication core(argc, argv);  // local sockets want one; it is gone again before the GUI's QApplication
        return nexus::ui::send_ipc_command(nexus::ui::kIpcSocketName, nexus::ui::kIpcClearClipboard);
    };
    if (const auto rc = nexus::app::run_cli(args, config, aliases, std::cout, std::cerr, clear_running_clipboard)) return *rc;

    QApplication app(argc, argv);
    app.setApplicationName("Nexus");
    app.setApplicationDisplayName("Nexus Launcher");
    app.setQuitOnLastWindowClosed(false);

    return nexus::ui::run_gui(app, args, config, aliases);
}
