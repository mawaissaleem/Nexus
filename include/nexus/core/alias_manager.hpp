#pragma once

#include <string>
#include <vector>
#include <mutex>

namespace nexus::core {

enum class AliasType {
    Url,
    Directory,
    File,
    Command
};

struct AliasEntry {
    std::string name;    // Nickname (e.g. "gh", "nexus", "docs")
    std::string target;  // Target link, path, or command
    AliasType type{AliasType::Command};
};

class AliasManager {
public:
    static AliasManager& instance();

    bool load(const std::string& custom_path = "");
    bool save();

    std::vector<AliasEntry> get_aliases() const;
    bool add_alias(const std::string& name, const std::string& target);
    bool remove_alias(const std::string& name);

    static AliasType detect_type(std::string_view target);

private:
    AliasManager();
    ~AliasManager() = default;

    void set_defaults();
    static std::string resolve_aliases_path();

    mutable std::mutex mutex_;
    std::string file_path_;
    std::vector<AliasEntry> aliases_;
};

} // namespace nexus::core
