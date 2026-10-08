#include "nexus/core/alias_manager.hpp"
#include "nexus/utils/json.hpp"
#include "nexus/utils/logger.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <algorithm>

namespace nexus::core {

namespace {

std::string expand_home(std::string_view path) {
    if (path.empty()) return "";
    if (path[0] == '~') {
        const char* home = std::getenv("HOME");
        if (home) {
            return std::string(home) + std::string(path.substr(1));
        }
    }
    return std::string(path);
}

} // namespace

AliasType AliasManager::detect_type(std::string_view target) {
    if (!target.empty() && target.front() == '>') return AliasType::Shell;
    if (target.rfind("http://", 0) == 0 || target.rfind("https://", 0) == 0 || target.rfind("www.", 0) == 0) {
        return AliasType::Url;
    }

    std::string expanded = expand_home(target);
    std::error_code ec;
    if (std::filesystem::is_directory(expanded, ec)) {
        return AliasType::Directory;
    }
    if (std::filesystem::is_regular_file(expanded, ec)) {
        return AliasType::File;
    }

    return AliasType::Command;
}

AliasManager& AliasManager::instance() {
    static AliasManager inst;
    return inst;
}

AliasManager::AliasManager() {
    file_path_ = resolve_aliases_path();
    set_defaults();
    load();
}

std::string AliasManager::resolve_aliases_path() {
    const char* xdg_config = std::getenv("XDG_CONFIG_HOME");
    std::filesystem::path dir;
    if (xdg_config && *xdg_config) {
        dir = std::filesystem::path(xdg_config) / "nexus";
    } else {
        const char* home = std::getenv("HOME");
        dir = home ? std::filesystem::path(home) / ".config" / "nexus" : std::filesystem::path("/tmp/nexus");
    }

    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) {
        return ".nexus_aliases.json";
    }

    return (dir / "aliases.json").string();
}

void AliasManager::set_defaults() {
    aliases_.clear();

    const char* home = std::getenv("HOME");
    std::string home_str = home ? home : "";

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

bool AliasManager::load(const std::string& custom_path) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!custom_path.empty()) {
        file_path_ = custom_path;
    }

    std::ifstream file(file_path_);
    if (!file.is_open()) {
        return false;
    }

    std::stringstream buf;
    buf << file.rdbuf();
    utils::JsonValue root;
    std::string error;
    if (!utils::parse_json(buf.str(), root, &error) || !root.is_array()) {
        const std::string backup = utils::preserve_malformed_file(file_path_, buf.str());
        NEXUS_LOG_ERROR("Malformed aliases file " + file_path_ + ": " +
                        (error.empty() ? "top level is not an array" : error) + "; using defaults, original kept at " +
                        (backup.empty() ? "nowhere (backup failed)" : backup));
        return false;
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
    return true;
}

bool AliasManager::save() {
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream file(file_path_);
    if (!file.is_open()) {
        file_path_ = ".nexus_aliases.json";
        file.open(file_path_);
        if (!file.is_open()) return false;
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

    return true;
}

std::vector<AliasEntry> AliasManager::get_aliases() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return aliases_;
}

bool AliasManager::add_alias(const std::string& name, const std::string& target) {
    if (name.empty() || target.empty()) return false;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(aliases_.begin(), aliases_.end(), [&](const AliasEntry& a) {
            return a.name == name;
        });

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

bool AliasManager::remove_alias(const std::string& name) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::remove_if(aliases_.begin(), aliases_.end(), [&](const AliasEntry& a) {
            return a.name == name;
        });
        if (it != aliases_.end()) {
            aliases_.erase(it, aliases_.end());
        } else {
            return false;
        }
    }
    return save();
}

} // namespace nexus::core
