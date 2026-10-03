#pragma once

#include <string>

namespace nexus::index {

// One row of the `files` table. `name` is the last path component.
struct FileEntry {
    std::string path;
    std::string name;
    bool is_dir{false};
    bool is_root{false};  // a configured search directory itself
};

} // namespace nexus::index
