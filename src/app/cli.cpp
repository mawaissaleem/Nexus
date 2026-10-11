#include "nexus/app/cli.hpp"
#include "nexus/app/services.hpp"
#include "nexus/core/query.hpp"
#include "nexus/index/database.hpp"
#include "nexus/utils/logger.hpp"
#include <atomic>
#include <chrono>
#include <iomanip>
#include <ostream>

namespace nexus::app {
namespace {

using Args = std::vector<std::string>;

void print_help(std::ostream& out, const std::string& shortcut) {
    out << "Nexus - Linux Productivity Launcher\n\n"
        << "Usage:\n"
        << "  nexus [options] [command]\n\n"
        << "Running without arguments starts the GUI launcher in the background.\n"
        << "Global hotkey: " << shortcut << " (configurable in settings)\n\n"
        << "Options:\n"
        << "  --toggle, -t           Toggle the launcher window (shows or hides running instance)\n"
        << "  --show                 Show the launcher window on the active monitor\n"
        << "  --hide                 Hide the launcher window\n"
        << "  --rebuild-index        Rescan system directories and search directories; rebuild the application and file indexes\n"
        << "  --list-apps            List all currently indexed desktop applications\n"
        << "  settings, config       Open Nexus Settings GUI (aliases, search directories, global shortcut)\n"
        << "  --settings             Open Nexus Settings GUI\n"
        << "  --help, -h             Show this help message\n\n"
        << "Directory Search Management:\n"
        << "  dir add <path>         Add a directory to file/folder search\n"
        << "  dir list               List configured search directories\n"
        << "  dir remove <path>      Remove a directory from search list\n\n"
        << "History:\n"
        << "  history clear          Delete recorded usage events and query history (indexes are kept)\n\n"
        << "Custom Aliases / Nicknames:\n"
        << "  alias add <name> <target>   Add nickname for URL, folder, or app (start target with > to run in a shell)\n"
        << "  alias list                  List configured aliases\n"
        << "  alias remove <name>         Remove an alias\n\n"
        << "CLI Search:\n"
        << "  query, -q <search-term>  Execute single search query and print ranked results\n";
}

void print_results(std::ostream& out, const std::vector<core::SearchResult>& results, double latency_ms) {
    out << "\nResults (" << results.size() << " matches found in "
        << std::fixed << std::setprecision(2) << latency_ms << " ms):\n";
    out << "------------------------------------------------------------\n";

    if (results.empty()) {
        out << "  No matching results.\n";
        return;
    }

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& res = results[i];
        out << " [" << (i + 1) << "] " << res.title;
        if (!res.subtitle.empty()) {
            out << " - " << res.subtitle;
        }
        out << "  (score: " << std::fixed << std::setprecision(1) << res.score
            << ", provider: " << res.provider_id << ")\n";
    }
    out << "------------------------------------------------------------\n";
}

int run_dir(const Args& args, core::ConfigManager& config, std::ostream& out, std::ostream& err) {
    const std::string sub = args.size() >= 2 ? args[1] : "";
    if (sub == "add" && args.size() >= 3) {
        if (!config.add_search_directory(args[2])) {
            err << "Failed: directory does not exist or is invalid: " << args[2] << '\n';
            return 1;
        }
        out << "Added search directory: " << args[2]
            << "\nRun `nexus --rebuild-index` (or restart Nexus) to index it.\n";
    } else if (sub == "remove" && args.size() >= 3) {
        if (!config.remove_search_directory(args[2])) {
            err << "Directory not found in search list: " << args[2] << '\n';
            return 1;
        }
        out << "Removed search directory: " << args[2]
            << "\nRun `nexus --rebuild-index` (or restart Nexus) to drop its files from search.\n";
    } else if (sub == "list") {
        const auto dirs = config.get_search_directories();
        out << "Configured search directories (" << dirs.size() << "):\n";
        for (const auto& d : dirs) out << " - " << d << '\n';
    } else {
        out << "Usage: nexus dir [add <path> | remove <path> | list]\n";
    }
    return 0;
}

int run_alias(const Args& args, core::AliasManager& aliases, std::ostream& out, std::ostream& err) {
    const std::string sub = args.size() >= 2 ? args[1] : "";
    if (sub == "add" && args.size() >= 4) {
        std::string target = args[3];
        for (size_t i = 4; i < args.size(); ++i) target += " " + args[i];  // unquoted multi-word target
        if (!aliases.add_alias(args[2], target)) {
            err << "Failed to save alias.\n";
            return 1;
        }
        out << "Saved alias: '" << args[2] << "' -> " << target << '\n';
    } else if (sub == "remove" && args.size() >= 3) {
        if (!aliases.remove_alias(args[2])) {
            err << "Alias not found: " << args[2] << '\n';
            return 1;
        }
        out << "Removed alias: " << args[2] << '\n';
    } else if (sub == "list") {
        const auto list = aliases.get_aliases();
        out << "Configured aliases (" << list.size() << "):\n";
        for (const auto& a : list) out << " - " << a.name << " -> " << a.target << '\n';
    } else {
        out << "Usage: nexus alias [add <name> <target> | remove <name> | list]\n";
    }
    return 0;
}

int run_history(const Args& args, std::ostream& out, std::ostream& err) {
    if (args.size() < 2 || args[1] != "clear") {
        out << "Usage: nexus history clear\n";
        return 0;
    }
    // Database only: no application scan, no providers.
    index::Database db;
    if (!db.open()) {
        NEXUS_LOG_WARN("Could not open local SQLite database. Running in-memory.");
    }
    const auto cleared = db.clear_history();
    if (!cleared) {
        err << "Failed to clear history.\n";
        return 1;
    }
    out << "Cleared " << cleared->usage_events << " usage events and " << cleared->queries
        << " query history entries.\n";
    if (!cleared->file_compacted) {
        err << "Could not compact the database file, so old entries may still be readable in it."
               " Run again when Nexus is closed.\n";
    }
    return 0;
}

double ms_since(std::chrono::high_resolution_clock::time_point start) {
    return std::chrono::duration<double, std::milli>(std::chrono::high_resolution_clock::now() - start).count();
}

int run_rebuild_index(Services& s, core::ConfigManager& config, std::ostream& out) {
    out << "Rebuilding desktop application index...\n";
    auto start = std::chrono::high_resolution_clock::now();
    const size_t count = s.app_indexer->rebuild_index();
    out << "Indexed " << count << " applications in " << std::fixed << std::setprecision(2) << ms_since(start)
        << " ms.\n";

    out << "Rebuilding file index...\n";
    start = std::chrono::high_resolution_clock::now();
    const std::atomic<bool> never_cancel{false};
    const size_t files = s.file_indexer->rebuild(file_index_options(config), never_cancel);
    out << "Indexed " << files << " files and folders in " << std::fixed << std::setprecision(2) << ms_since(start)
        << " ms.\n";
    return 0;
}

int run_list_apps(Services& s, std::ostream& out) {
    s.app_indexer->rebuild_index();  // cache may predate apps installed while Nexus was off
    const auto apps = s.app_indexer->get_applications();
    out << "Indexed applications (" << apps->size() << "):\n";
    for (const auto& a : *apps) out << " - " << a.name << " [" << a.id << "] -> " << a.exec << '\n';
    return 0;
}

int run_query(const Args& args, Services& s, std::ostream& out) {
    std::string query_str = args[1];
    for (size_t i = 2; i < args.size(); ++i) query_str += " " + args[i];

    s.app_indexer->rebuild_index();  // cache may predate apps installed while Nexus was off
    const auto start = std::chrono::high_resolution_clock::now();
    const auto results = s.engine->search(core::Query(query_str), 10);
    print_results(out, results, ms_since(start));
    return 0;
}

} // namespace

std::optional<int> run_cli(const Args& args, core::ConfigManager& config, core::AliasManager& aliases,
                           std::ostream& out, std::ostream& err) {
    if (args.empty()) return std::nullopt;
    const std::string& cmd = args[0];

    if (cmd == "--help" || cmd == "-h") {
        print_help(out, config.get_shortcut());
        return 0;
    }
    if (cmd == "dir") return run_dir(args, config, out, err);
    if (cmd == "alias") return run_alias(args, aliases, out, err);
    if (cmd == "history") return run_history(args, out, err);

    const bool needs_services = cmd == "--rebuild-index" || cmd == "--list-apps" || cmd == "query" || cmd == "-q";
    if (!needs_services) return std::nullopt;

    const bool is_query = cmd == "query" || cmd == "-q";
    if (is_query && args.size() < 2) {  // checked before the indexers are built
        err << "Error: query requires a search term.\n";
        return 1;
    }

    Services s = make_services(config, aliases);
    if (cmd == "--rebuild-index") return run_rebuild_index(s, config, out);
    if (cmd == "--list-apps") return run_list_apps(s, out);
    return run_query(args, s, out);
}

} // namespace nexus::app
