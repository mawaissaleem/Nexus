#pragma once

#include "nexus/providers/plugin_protocol.hpp"

#include <filesystem>
#include <set>
#include <string>
#include <vector>

namespace nexus::providers {

// `$XDG_DATA_HOME/nexus/plugins` (default ~/.local/share/nexus/plugins), then `nexus/plugins` under each
// `$XDG_DATA_DIRS` entry (default /usr/local/share:/usr/share). Earlier entries win on the same id.
[[nodiscard]] std::vector<std::filesystem::path> plugin_search_dirs();

// Valid manifests of every `<root>/<id>/` in `roots` (earlier root wins; a shadowed copy is skipped with an
// INFO log). Invalid manifests, ids in `reserved` and duplicates are skipped with a WARN; the rest still load.
// Only reads manifests: nothing is spawned.
[[nodiscard]] std::vector<plugin::Manifest> discover_plugins(const std::vector<std::filesystem::path>& roots,
                                                             const std::set<std::string>& reserved);

} // namespace nexus::providers
