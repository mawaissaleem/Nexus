#pragma once

#include "nexus/utils/status.hpp"
#include <string>
#include <vector>
#include <mutex>

namespace nexus::core {

enum class AliasType {
    Url,
    Directory,
    File,
    Command,  // run directly, no shell
    Shell     // target starts with ">": the only alias type that runs through /bin/sh
};

struct AliasEntry {
    std::string name;    // Nickname (e.g. "gh", "nexus", "docs")
    std::string target;  // Target link, path, or command
    AliasType type{AliasType::Command};
};

class AliasManager {
public:
    // Loads `aliases_path` (defaults if missing). Production passes resolve_aliases_path(); tests pass a temp file.
    explicit AliasManager(const std::string& aliases_path);
    static std::string resolve_aliases_path();

    // Each of these returns a Status whose message says why it failed (missing or unwritable file, unknown alias, ...).
    utils::Status load(const std::string& custom_path = "");
    utils::Status save();

    std::vector<AliasEntry> get_aliases() const;
    utils::Status add_alias(const std::string& name, const std::string& target);
    utils::Status remove_alias(const std::string& name);

    static AliasType detect_type(std::string_view target);

private:
    void set_defaults();

    mutable std::mutex mutex_;
    std::string file_path_;
    std::vector<AliasEntry> aliases_;
};

}  // namespace nexus::core
