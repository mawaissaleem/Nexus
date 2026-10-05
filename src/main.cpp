#include "nexus/core/search_engine.hpp"
#include "nexus/core/ranking_engine.hpp"
#include "nexus/core/query.hpp"
#include "nexus/index/database.hpp"
#include "nexus/index/app_indexer.hpp"
#include "nexus/index/file_indexer.hpp"
#include "nexus/index/file_watcher.hpp"
#include "nexus/providers/app_provider.hpp"
#include "nexus/providers/calc_provider.hpp"
#include "nexus/providers/shell_provider.hpp"
#include "nexus/providers/file_provider.hpp"
#include "nexus/providers/alias_provider.hpp"
#include "nexus/core/config_manager.hpp"
#include "nexus/core/alias_manager.hpp"
#include "nexus/ui/launcher_window.hpp"
#include "nexus/ui/settings_dialog.hpp"
#include "nexus/ui/global_shortcut.hpp"
#include "nexus/utils/logger.hpp"
#include "nexus/utils/shortcut.hpp"
#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QSystemTrayIcon>
#include <QMenu>
#include <QIcon>
#include <QMessageBox>
#include <atomic>
#include <functional>
#include <utility>
#include <thread>
#include <iostream>
#include <string>
#include <chrono>
#include <iomanip>

namespace {

const char* IPC_SOCKET_NAME = "nexus_launcher_ipc";

void print_help() {
    std::cout << "Nexus - Linux Productivity Launcher\n\n"
              << "Usage:\n"
              << "  nexus [options] [command]\n\n"
              << "Running without arguments starts the GUI launcher in the background.\n"
              << "Global hotkey: " << nexus::core::ConfigManager::instance().get_shortcut() << " (configurable in settings)\n\n"
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
              << "  alias add <name> <target>   Add nickname for URL, folder, or app\n"
              << "  alias list                  List configured aliases\n"
              << "  alias remove <name>         Remove an alias\n\n"
              << "CLI Search:\n"
              << "  query, -q <search-term>  Execute single search query and print ranked results\n";
}

void print_results(const std::vector<nexus::core::SearchResult>& results, double latency_ms) {
    std::cout << "\nResults (" << results.size() << " matches found in "
              << std::fixed << std::setprecision(2) << latency_ms << " ms):\n";
    std::cout << "------------------------------------------------------------\n";

    if (results.empty()) {
        std::cout << "  No matching results.\n";
        return;
    }

    for (size_t i = 0; i < results.size(); ++i) {
        const auto& res = results[i];
        std::cout << " [" << (i + 1) << "] " << res.title;
        if (!res.subtitle.empty()) {
            std::cout << " - " << res.subtitle;
        }
        std::cout << "  (score: " << std::fixed << std::setprecision(1) << res.score
                  << ", provider: " << res.provider_id << ")\n";
    }
    std::cout << "------------------------------------------------------------\n";
}

} // namespace

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("Nexus");
    app.setApplicationDisplayName("Nexus Launcher");
    app.setQuitOnLastWindowClosed(false);

    nexus::utils::Logger::instance().set_level(nexus::utils::LogLevel::Info);

    bool start_with_settings = false;

    // 1. Check for quick CLI commands that don't need GUI/daemon
    if (argc > 1) {
        std::string arg1 = argv[1];

        if (arg1 == "--help" || arg1 == "-h") {
            print_help();
            return 0;
        }

        // IPC client: Check if another instance is already running
        if (arg1 == "--toggle" || arg1 == "-t" || arg1 == "--show" || arg1 == "--hide" ||
            arg1 == "--settings" || arg1 == "settings" || arg1 == "config") {
            QLocalSocket socket;
            socket.connectToServer(IPC_SOCKET_NAME);
            if (socket.waitForConnected(200)) {
                std::string cmd;
                if (arg1 == "--hide") cmd = "HIDE";
                else if (arg1 == "--show") cmd = "SHOW";
                else if (arg1 == "--settings" || arg1 == "settings" || arg1 == "config") cmd = "SETTINGS";
                else cmd = "TOGGLE";
                socket.write(cmd.c_str(), cmd.size());
                socket.flush();
                socket.waitForBytesWritten(200);
                return 0;
            }
            // If server not running and arg is settings, mark flag to launch settings directly!
            if (arg1 == "--settings" || arg1 == "settings" || arg1 == "config") {
                start_with_settings = true;
            }
        }
    } else {
        // If launched with no args, check if an instance is already running to toggle it
        QLocalSocket socket;
        socket.connectToServer(IPC_SOCKET_NAME);
        if (socket.waitForConnected(200)) {
            socket.write("TOGGLE", 6);
            socket.flush();
            socket.waitForBytesWritten(200);
            return 0;
        }
    }

    // Initialize Database and AppIndexer
    auto db = std::make_shared<nexus::index::Database>();
    if (!db->open()) {
        NEXUS_LOG_WARN("Could not open local SQLite database. Running in-memory.");
    }

    auto app_indexer = std::make_shared<nexus::index::AppIndexer>(db);
    app_indexer->initialize();

    auto file_indexer = std::make_shared<nexus::index::FileIndexer>(db);
    // Read config on the calling thread so worker threads never race the Settings dialog.
    const auto file_options = []() {
        auto& cfg = nexus::core::ConfigManager::instance();
        return nexus::index::FileIndexOptions{cfg.get_search_directories(), cfg.get_file_exclusions()};
    };

    // Setup SearchEngine and Providers
    auto engine = std::make_shared<nexus::core::SearchEngine>();
    engine->register_provider(std::make_shared<nexus::providers::AliasProvider>());
    engine->register_provider(std::make_shared<nexus::providers::ApplicationProvider>(
        app_indexer, db, [] { return nexus::core::ConfigManager::instance().is_history_enabled(); }));
    engine->register_provider(std::make_shared<nexus::providers::FileProvider>(db));
    engine->register_provider(std::make_shared<nexus::providers::CalculatorProvider>());
    engine->register_provider(std::make_shared<nexus::providers::ShellProvider>());

    // Providers switched off in Settings are skipped per search, so the toggle needs no restart.
    engine->set_provider_filter([](const std::string& id) {
        return nexus::core::ConfigManager::instance().is_provider_enabled(id);
    });
    {
        std::vector<std::pair<std::string, std::string>> provider_names;
        for (const auto& p : engine->providers()) provider_names.emplace_back(p->id(), p->name());
        nexus::ui::SettingsDialog::set_providers(std::move(provider_names));
    }

    // CLI indexing / search / config commands
    if (argc > 1) {
        std::string arg1 = argv[1];

        // --- Directory Search Configuration ---
        if (arg1 == "dir") {
            if (argc >= 3 && std::string(argv[2]) == "add" && argc >= 4) {
                if (nexus::core::ConfigManager::instance().add_search_directory(argv[3])) {
                    std::cout << "Added search directory: " << argv[3]
                              << "\nRun `nexus --rebuild-index` (or restart Nexus) to index it.\n";
                } else {
                    std::cerr << "Failed: directory does not exist or is invalid: " << argv[3] << '\n';
                    return 1;
                }
                return 0;
            } else if (argc >= 3 && std::string(argv[2]) == "remove" && argc >= 4) {
                if (nexus::core::ConfigManager::instance().remove_search_directory(argv[3])) {
                    std::cout << "Removed search directory: " << argv[3]
                              << "\nRun `nexus --rebuild-index` (or restart Nexus) to drop its files from search.\n";
                } else {
                    std::cerr << "Directory not found in search list: " << argv[3] << '\n';
                    return 1;
                }
                return 0;
            } else if (argc >= 3 && std::string(argv[2]) == "list") {
                auto dirs = nexus::core::ConfigManager::instance().get_search_directories();
                std::cout << "Configured search directories (" << dirs.size() << "):\n";
                for (const auto& d : dirs) {
                    std::cout << " - " << d << '\n';
                }
                return 0;
            } else {
                std::cout << "Usage: nexus dir [add <path> | remove <path> | list]\n";
                return 0;
            }
        }

        // --- History ---
        if (arg1 == "history") {
            if (argc >= 3 && std::string(argv[2]) == "clear") {
                const auto cleared = db->clear_history();
                if (!cleared) {
                    std::cerr << "Failed to clear history.\n";
                    return 1;
                }
                std::cout << "Cleared " << cleared->usage_events << " usage events and " << cleared->queries
                          << " query history entries.\n";
                return 0;
            }
            std::cout << "Usage: nexus history clear\n";
            return 0;
        }

        // --- Aliases / Nicknames Configuration ---
        if (arg1 == "alias") {
            if (argc >= 3 && std::string(argv[2]) == "add" && argc >= 5) {
                if (nexus::core::AliasManager::instance().add_alias(argv[3], argv[4])) {
                    std::cout << "Saved alias: '" << argv[3] << "' -> " << argv[4] << '\n';
                } else {
                    std::cerr << "Failed to save alias.\n";
                    return 1;
                }
                return 0;
            } else if (argc >= 3 && std::string(argv[2]) == "remove" && argc >= 4) {
                if (nexus::core::AliasManager::instance().remove_alias(argv[3])) {
                    std::cout << "Removed alias: " << argv[3] << '\n';
                } else {
                    std::cerr << "Alias not found: " << argv[3] << '\n';
                    return 1;
                }
                return 0;
            } else if (argc >= 3 && std::string(argv[2]) == "list") {
                auto aliases = nexus::core::AliasManager::instance().get_aliases();
                std::cout << "Configured aliases (" << aliases.size() << "):\n";
                for (const auto& a : aliases) {
                    std::cout << " - " << a.name << " -> " << a.target << '\n';
                }
                return 0;
            } else {
                std::cout << "Usage: nexus alias [add <name> <target> | remove <name> | list]\n";
                return 0;
            }
        }

        if (arg1 == "--rebuild-index") {
            std::cout << "Rebuilding desktop application index...\n";
            auto start = std::chrono::high_resolution_clock::now();
            size_t count = app_indexer->rebuild_index();
            auto end = std::chrono::high_resolution_clock::now();
            double dur = std::chrono::duration<double, std::milli>(end - start).count();
            std::cout << "Indexed " << count << " applications in "
                      << std::fixed << std::setprecision(2) << dur << " ms.\n";

            std::cout << "Rebuilding file index...\n";
            start = std::chrono::high_resolution_clock::now();
            const std::atomic<bool> never_cancel{false};
            size_t files = file_indexer->rebuild(file_options(), never_cancel);
            end = std::chrono::high_resolution_clock::now();
            dur = std::chrono::duration<double, std::milli>(end - start).count();
            std::cout << "Indexed " << files << " files and folders in "
                      << std::fixed << std::setprecision(2) << dur << " ms.\n";
            return 0;
        }

        if (arg1 == "--list-apps") {
            auto apps = app_indexer->get_applications();
            std::cout << "Indexed applications (" << apps->size() << "):\n";
            for (const auto& a : *apps) {
                std::cout << " - " << a.name << " [" << a.id << "] -> " << a.exec << '\n';
            }
            return 0;
        }

        if (arg1 == "query" || arg1 == "-q") {
            if (argc < 3) {
                std::cerr << "Error: query requires a search term.\n";
                return 1;
            }
            std::string query_str;
            for (int i = 2; i < argc; ++i) {
                if (i > 2) query_str += " ";
                query_str += argv[i];
            }

            auto start = std::chrono::high_resolution_clock::now();
            nexus::core::Query query(query_str);
            auto results = engine->search(query, 10);
            auto end = std::chrono::high_resolution_clock::now();
            double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

            print_results(results, latency_ms);
            return 0;
        }
    }

    // New/removed .desktop files update the app index on the watcher's own thread (a rebuild is ~5 ms, so a
    // full app rescan per change; the file index is left alone). Stopped (joined) on quit below.
    app_indexer->start_watching();

    // First (and every startup) file index build runs off the UI thread. Until the very first build lands,
    // `file ` queries return nothing; afterwards they keep serving the previous index while a rebuild runs.
    std::atomic<bool> quitting{false};
    // One index build at a time (UI-thread state): a request during a build is remembered and run once it finishes,
    // so a build with an older directory list can never overwrite a newer one.
    bool rebuilding = true;
    bool rebuild_requested = false;
    std::function<void()> start_rebuild;  // set once the tray menu exists, before the event loop runs
    const auto rebuild_finished = [&]() {
        rebuilding = false;
        if (std::exchange(rebuild_requested, false)) start_rebuild();
    };
    // Live updates between rebuilds. Every rebuild is bracketed by arm()/resume() (see FileWatcher); the rebuild
    // threads are serialised above, so arm/resume never overlap. A kernel event overflow asks for a full rebuild.
    nexus::index::FileWatcher file_watcher(db, [&]() {
        QMetaObject::invokeMethod(&app, [&]() { start_rebuild(); }, Qt::QueuedConnection);
    });
    std::thread startup_index_thread([&, options = file_options()]() {
        file_watcher.arm(options, quitting);
        file_indexer->rebuild(options, quitting);
        file_watcher.resume();
        QMetaObject::invokeMethod(&app, rebuild_finished, Qt::QueuedConnection);
    });

    // Initialize GUI Launcher Window
    nexus::ui::ThemeController theme(nexus::core::ConfigManager::instance().get_theme());
    nexus::ui::SettingsDialog::set_theme_controller(&theme);
    auto launcher = std::make_shared<nexus::ui::LauncherWindow>(engine, theme);

    // 2. Start Single-Instance IPC Server for instant toggle from shortcuts
    auto ipc_server = std::make_unique<QLocalServer>(&app);
    QLocalServer::removeServer(IPC_SOCKET_NAME); // Clean any previous stale socket file
    if (ipc_server->listen(IPC_SOCKET_NAME)) {
        NEXUS_LOG_INFO("Nexus IPC server listening on: " + std::string(IPC_SOCKET_NAME));
        QObject::connect(ipc_server.get(), &QLocalServer::newConnection, [server_ptr = ipc_server.get(), launcher]() {
            QLocalSocket* client = server_ptr->nextPendingConnection();
            if (!client) return;
            QObject::connect(client, &QLocalSocket::readyRead, [client, launcher]() {
                QString msg = QString::fromUtf8(client->readAll()).trimmed();
                if (msg == "TOGGLE") {
                    launcher->toggle_launcher();
                } else if (msg == "SHOW") {
                    launcher->show_launcher();
                } else if (msg == "HIDE") {
                    launcher->hide_launcher();
                } else if (msg == "SETTINGS") {
                    nexus::ui::SettingsDialog::show_settings();
                }
            });
        });
    } else {
        NEXUS_LOG_WARN("Could not start IPC server: " + ipc_server->errorString().toStdString());
    }

    // 3. Global Hotkey Manager (Native X11 listener for X11 sessions)
    auto shortcut_mgr = std::make_unique<nexus::ui::GlobalShortcutManager>();
    QObject::connect(shortcut_mgr.get(), &nexus::ui::GlobalShortcutManager::activated, [&launcher]() {
        launcher->toggle_launcher();
    });
    // On Wayland the single tray notice below covers it; don't stack a second popup for the same cause.
    const bool wayland = nexus::utils::is_wayland_session();
    QObject::connect(shortcut_mgr.get(), &nexus::ui::GlobalShortcutManager::grab_failed, [wayland](const QString& msg) {
        if (wayland) {
            NEXUS_LOG_WARN("Global shortcut grab failed: " + msg.toStdString());
            return;
        }
        QMessageBox::warning(nullptr, "Nexus global shortcut", msg + "\nChoose another one in Settings.");
    });
    shortcut_mgr->apply_shortcut(nexus::core::ConfigManager::instance().get_shortcut());

    // 4. System Tray Icon
    QSystemTrayIcon tray_icon;
    QIcon icon = QIcon::fromTheme("system-search");
    if (icon.isNull()) {
        icon = QIcon::fromTheme("application-x-executable");
    }
    tray_icon.setIcon(icon);
    const auto shortcut_text = []() { return QString::fromStdString(nexus::core::ConfigManager::instance().get_shortcut()); };
    tray_icon.setToolTip("Nexus Launcher (" + shortcut_text() + ")");

    QMenu tray_menu;
    auto* toggle_action = tray_menu.addAction("Toggle Launcher (" + shortcut_text() + ")");
    QObject::connect(toggle_action, &QAction::triggered, [&launcher]() {
        launcher->toggle_launcher();
    });

    // Settings dialog applies a new shortcut: persist, re-grab, refresh labels.
    nexus::ui::SettingsDialog::set_shortcut_applier([&](const std::string& text) {
        if (!nexus::core::ConfigManager::instance().set_shortcut(text)) return false;
        tray_icon.setToolTip("Nexus Launcher (" + shortcut_text() + ")");
        toggle_action->setText("Toggle Launcher (" + shortcut_text() + ")");
        return shortcut_mgr->apply_shortcut(text);
    });

    auto* settings_action = tray_menu.addAction("Settings...");
    QObject::connect(settings_action, &QAction::triggered, []() {
        nexus::ui::SettingsDialog::show_settings();
    });

    auto* rebuild_action = tray_menu.addAction("Rebuild Indexes");
    // Rescan off the UI thread; see `rebuilding` above for how overlapping requests are serialised.
    std::thread rebuild_thread;
    start_rebuild = [&]() {
        if (rebuilding) {
            rebuild_requested = true;
            return;
        }
        rebuilding = true;
        if (rebuild_thread.joinable()) rebuild_thread.join();  // previous worker already finished
        tray_icon.showMessage("Nexus", "Rebuilding application and file indexes...", QSystemTrayIcon::Information, 3000);
        rebuild_thread = std::thread([&, indexer = app_indexer, options = file_options()]() {
            size_t count = indexer->rebuild_index();
            file_watcher.arm(options, quitting);
            size_t files = file_indexer->rebuild(options, quitting);
            file_watcher.resume();
            QMetaObject::invokeMethod(&tray_icon, [&, count, files]() {
                tray_icon.showMessage("Nexus", QString("Indexes rebuilt (%1 apps, %2 files)").arg(count).arg(files),
                                      QSystemTrayIcon::Information, 3000);
                rebuild_finished();
            }, Qt::QueuedConnection);
        });
    };
    QObject::connect(rebuild_action, &QAction::triggered, [&]() { start_rebuild(); });
    // Search directories added/removed in Settings take effect without a restart.
    nexus::ui::SettingsDialog::set_directories_changed_handler([&]() { start_rebuild(); });

    tray_menu.addSeparator();

    auto* quit_action = tray_menu.addAction("Quit");
    QObject::connect(quit_action, &QAction::triggered, [&app, &shortcut_mgr]() {
        shortcut_mgr->stop();
        app.quit();
    });

    tray_icon.setContextMenu(&tray_menu);
    QObject::connect(&tray_icon, &QSystemTrayIcon::activated, [&launcher](QSystemTrayIcon::ActivationReason reason) {
        if (reason == QSystemTrayIcon::Trigger) {
            launcher->toggle_launcher();
        }
    });

    const bool tray_available = QSystemTrayIcon::isSystemTrayAvailable();
    if (tray_available) {
        tray_icon.show();
    }

    // Once per process start. Without a tray only the log line is shown.
    if (wayland) {
        const QString hint = "Global shortcuts don't work on Wayland. Bind `nexus --toggle` to a key in your compositor settings.";
        NEXUS_LOG_WARN(hint.toStdString());
        if (tray_available) {
            tray_icon.showMessage("Nexus", hint, QSystemTrayIcon::Warning, 10000);
        }
    }

    // Show initially so user sees it right away on first run
    if (start_with_settings) {
        nexus::ui::SettingsDialog::show_settings();
    } else {
        launcher->show_launcher();
    }

    NEXUS_LOG_INFO("Nexus is running in the background. Press " + nexus::core::ConfigManager::instance().get_shortcut() + " to toggle.");
    const int rc = app.exec();
    quitting = true;  // walkers poll this, so quit mid-build returns within one directory entry
    if (startup_index_thread.joinable()) startup_index_thread.join();
    if (rebuild_thread.joinable()) rebuild_thread.join();  // quit during a rebuild: wait, never destroy a joinable thread
    file_watcher.stop();
    app_indexer->stop_watching();
    return rc;
}
