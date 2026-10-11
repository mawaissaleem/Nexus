#pragma once

#include "nexus/core/alias_manager.hpp"
#include "nexus/core/config_manager.hpp"
#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace nexus::app {

// Runs a command-line subcommand (`args` is argv without the program name) and returns its exit code,
// or nullopt when `args` is not one of them (no arguments, --toggle, settings, anything unknown): start the GUI.
// `help`, `dir` and `alias` touch only config/aliases; `history clear` opens only the database;
// `query`, `--list-apps` and `--rebuild-index` also build the indexers.
[[nodiscard]] std::optional<int> run_cli(const std::vector<std::string>& args,
                                         core::ConfigManager& config,
                                         core::AliasManager& aliases,
                                         std::ostream& out,
                                         std::ostream& err);

} // namespace nexus::app
