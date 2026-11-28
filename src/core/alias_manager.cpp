#include "nexus/core/alias_manager.hpp"
#include "nexus/utils/json.hpp"
#include "nexus/utils/logger.hpp"
#include "nexus/utils/xdg.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cerrno>
#include <cstdlib>
#include <algorithm>

namespace nexus::core {

AliasType AliasManager::detect_type(std::string_view target) {
    if (!target.empty() && target.front() == '>') return AliasType::Shell;
    for (const std::string_view prefix : {"http://", "https://", "www."}) {
        if (target.rfind(prefix, 0) == 0) return AliasType::Url;
    }

    // `~` with no usable HOME can't name a file: stays a plain command rather than a cwd-relative path.
    const std::optional<std::string> expanded = utils::expand_home(target);
    if (!expanded) return AliasType::Command;

    std::error_code ec;
    if (std::filesystem::is_directory(*expanded, ec)) {
        return AliasType::Directory;
    }
    if (std::filesystem::is_regular_file(*expanded, ec)) {
        return AliasType::File;
    }

    return AliasType::Command;
}

AliasManager::AliasManager(const std::string& aliases_path) : file_path_(aliases_path) {
    set_defaults();
    static_cast<void>(load());  // a missing file means built-in aliases; a malformed one is logged inside load()
}

std::string AliasManager::resolve_aliases_path() {
    const std::filesystem::path base = utils::xdg_user_dir("XDG_CONFIG_HOME", ".config");
    if (base.empty()) {
        NEXUS_LOG_WARN("No usable config directory (HOME and XDG_CONFIG_HOME unset or relative); "
                       "using built-in aliases, aliases not saved");
        return "";
    }

    const std::filesystem::path dir = base / "nexus";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        NEXUS_LOG_WARN("Cannot create " + dir.string() + ": " + ec.message() +
                       "; using built-in aliases, aliases not saved");
        return "";
    }

    return (dir / "aliases.json").string();
}

void AliasManager::set_defaults() {
    aliases_.clear();

    const std::string home_str = utils::home_dir().string();

    std::error_code ec;
    auto cwd = std::filesystem::current_path(ec).string();

    aliases_.push_back({"nexus", cwd, AliasType::Directory});
    aliases_.push_back({"gh", "https://github.com", AliasType::Url});
    aliases_.push_back({"google", "https://google.com", AliasType::Url});

    if (!home_str.empty()) {
        aliases_.push_back({"docs", home_str + "/Documents", AliasType::Directory});
        aliases_.push_back({"downloads", home_str + "/Downloads", AliasType::Directory});
    }
}

utils::Status AliasManager::load(const std::string& custom_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!custom_path.empty()) {
        file_path_ = custom_path;
    }

    std::ifstream file(file_path_);
    if (!file.is_open()) {
        return utils::Status::from_errno("can't read " + file_path_, errno);
    }

    std::stringstream buf;
    buf << file.rdbuf();
    utils::JsonValue root;
    std::string error;
    if (!utils::parse_json(buf.str(), root, &error) || !root.is_array()) {
        const std::string backup = utils::preserve_malformed_file(file_path_, buf.str());
        const std::string reason = error.empty() ? "top level is not an array" : error;
        const std::string kept_at = backup.empty() ? "nowhere (backup failed)" : backup;
        NEXUS_LOG_ERROR("Malformed aliases file " + file_path_ + ": " + reason + "; using defaults, original kept at " +
                        kept_at);
        return utils::Status::error("can't use " + file_path_ + ": " + reason +
                                    " (using built-in aliases, the original is kept at " + kept_at + ")");
    }

    std::vector<AliasEntry> loaded;
    for (const auto& item : root.array) {
        const auto* name = item.is_object() ? item.find("name") : nullptr;
        const auto* target = item.is_object() ? item.find("target") : nullptr;
        if (!name || !target || !name->is_string() || !target->is_string()) continue;
        if (name->string.empty() || target->string.empty()) continue;
        AliasEntry entry;
        entry.name = name->string;
        entry.target = target->string;
        entry.type = detect_type(entry.target);
        loaded.push_back(std::move(entry));
    }

    if (!loaded.empty()) {
        aliases_ = std::move(loaded);
    }
    return {};
}

utils::Status AliasManager::save() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_path_.empty()) {
        NEXUS_LOG_ERROR("No config directory; aliases not saved");
        return utils::Status::error("can't save aliases: there is no usable config directory");
    }
    std::ofstream file(file_path_);
    if (!file.is_open()) {
        const utils::Status status = utils::Status::from_errno("can't write " + file_path_, errno);
        NEXUS_LOG_ERROR(status.message());
        return status;
    }

    file << "[\n";
    for (size_t i = 0; i < aliases_.size(); ++i) {
        file << "  {\n";
        file << "    \"name\": " << utils::json_quote(aliases_[i].name) << ",\n";
        file << "    \"target\": " << utils::json_quote(aliases_[i].target) << "\n";
        file << "  }";
        if (i + 1 < aliases_.size()) {
            file << ",";
        }
        file << "\n";
    }
    file << "]\n";

    // Close now so a write that failed late (disk full) is caught here and not lost in the destructor.
    file.close();
    if (file.fail()) {
        const utils::Status status = utils::Status::from_errno("can't write " + file_path_, errno);
        NEXUS_LOG_ERROR(status.message());
        return status;
    }
    return {};
}

std::vector<AliasEntry> AliasManager::get_aliases() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return aliases_;
}

utils::Status AliasManager::add_alias(const std::string& name, const std::string& target) {
    if (name.empty() || target.empty()) return utils::Status::error("an alias needs both a name and a target");

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(aliases_.begin(), aliases_.end(), [&](const AliasEntry& a) { return a.name == name; });

        if (it != aliases_.end()) {
            it->target = target;
            it->type = detect_type(target);
        } else {
            AliasEntry entry;
            entry.name = name;
            entry.target = target;
            entry.type = detect_type(target);
            aliases_.push_back(std::move(entry));
        }
    }

    return save();
}

utils::Status AliasManager::remove_alias(const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(aliases_.begin(), aliases_.end(), [&](const AliasEntry& a) { return a.name == name; });
        if (it != aliases_.end()) {
            aliases_.erase(it, aliases_.end());
        } else {
            return utils::Status::error("there is no alias named \"" + name + "\"");
        }
    }
    return save();
}

}  // namespace nexus::core
