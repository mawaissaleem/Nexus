#pragma once

#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include <functional>
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace nexus::app {

// The commands that need no config or aliases: --help, --version and the unknown-argument error.
// Returns their exit code, or nullopt for anything else. main calls this before building the
// managers, so these paths never create or rewrite files in the config dir.
[[nodiscard]] std::optional<int> run_info_cli(const std::vector<std::string>& args, std::ostream& out,
                                              std::ostream& err);

// Runs a command-line subcommand (`args` is argv without the program name) and returns its exit code,
// or nullopt when `args` is not one of them (no arguments, --toggle, settings, --hide, Qt options): start the GUI.
// Anything else unknown prints an error plus usage and returns 1.
// Starts with run_info_cli. `dir` and `alias` touch only config/aliases; `history clear` opens only the database;
// `query`, `--list-apps` and `--rebuild-index` also build the indexers.
[[nodiscard]] std::optional<int> run_cli(const std::vector<std::string>& args, core::ConfigManager& config,
                                         core::AliasManager& aliases, std::ostream& out, std::ostream& err,
                                         // `history clear` calls it to tell a running launcher to empty its clipboard
                                         // history; true = one received it. Empty = no launcher to tell.
                                         std::function<bool()> clear_running_clipboard = {});

}  // namespace nexus::app
