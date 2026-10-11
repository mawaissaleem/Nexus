#pragma once

#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include <string>
#include <vector>

class QApplication;

namespace nexus::ui {

// Everything that is not a CLI subcommand: forwards --toggle/--show/--hide/--settings to a running instance
// and exits, or starts the launcher, tray, IPC server and indexers and runs the event loop. Returns the exit code.
// `config` and `aliases` must outlive the call.
int run_gui(QApplication& app, const std::vector<std::string>& args,
            core::ConfigManager& config, core::AliasManager& aliases);

} // namespace nexus::ui
