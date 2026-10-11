#include "nexus/ui/gui_app.hpp"
#include "nexus/app/services.hpp"
#include "nexus/index/file_watcher.hpp"
#include "nexus/ui/global_shortcut.hpp"
#include "nexus/ui/launcher_window.hpp"
#include "nexus/ui/settings_dialog.hpp"
#include "nexus/utils/logger.hpp"
#include "nexus/utils/shortcut.hpp"
#include <QApplication>
#include <QIcon>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <atomic>
#include <functional>
#include <memory>
#include <thread>
#include <utility>

namespace nexus::ui {
namespace {

const char* IPC_SOCKET_NAME = "nexus_launcher_ipc";

bool is_settings_arg(const std::string& a) { return a == "--settings" || a == "settings" || a == "config"; }

// Single-instance client: hands `cmd` to a running instance. False when none is listening.
bool send_to_running_instance(const char* cmd) {
    QLocalSocket socket;
    socket.connectToServer(IPC_SOCKET_NAME);
    if (!socket.waitForConnected(200)) return false;
    socket.write(cmd);
    socket.flush();
    socket.waitForBytesWritten(200);
    return true;
}

// Owns the launcher, tray, IPC server, shortcut grab, settings dialog, watchers and index threads.
// Member order is destruction order in reverse: threads and watchers outlive the widgets their callbacks touch.
class GuiApp {
public:
    GuiApp(QApplication& app, core::ConfigManager& config, core::AliasManager& aliases)
        : app_(app), config_(config), aliases_(aliases),
          services_(app::make_services(config, aliases)),
          file_watcher_(services_.db, [this]() {
              QMetaObject::invokeMethod(&app_, [this]() { start_rebuild(); }, Qt::QueuedConnection);
          }),
          theme_(config.get_theme()),
          launcher_(std::make_unique<LauncherWindow>(services_.engine, theme_, config)) {
        // New/removed .desktop files update the app index on the watcher's own thread (a rebuild is ~5 ms, so a
        // full app rescan per change; the file index is left alone). Stopped (joined) in run().
        services_.app_indexer->start_watching();

        // First (and every startup) file index build runs off the UI thread. Until the very first build lands,
        // `file ` queries return nothing; afterwards they keep serving the previous index while a rebuild runs.
        startup_index_thread_ = std::thread([this, options = app::file_index_options(config_)]() {
            // Apps installed while Nexus was off. After start_watching(), so an install mid-scan is caught by one or the other.
            services_.app_indexer->rebuild_index();
            file_watcher_.arm(options, quitting_);
            services_.file_indexer->rebuild(options, quitting_);
            file_watcher_.resume();
            QMetaObject::invokeMethod(&app_, [this]() { rebuild_finished(); }, Qt::QueuedConnection);
        });

        QObject::connect(launcher_.get(), &LauncherWindow::settings_requested, [this]() { show_settings(); });
        setup_ipc();
        setup_shortcut();
        setup_tray();
    }

    int run(bool start_with_settings) {
        // Show initially so user sees it right away on first run
        if (start_with_settings) {
            show_settings();
        } else {
            launcher_->show_launcher();
        }

        NEXUS_LOG_INFO("Nexus is running in the background. Press " + config_.get_shortcut() + " to toggle.");
        const int rc = app_.exec();
        quitting_ = true;  // walkers poll this, so quit mid-build returns within one directory entry
        if (startup_index_thread_.joinable()) startup_index_thread_.join();
        if (rebuild_thread_.joinable()) rebuild_thread_.join();  // quit during a rebuild: wait, never destroy a joinable thread
        file_watcher_.stop();
        services_.app_indexer->stop_watching();
        return rc;
    }

private:
    QString shortcut_text() const { return QString::fromStdString(config_.get_shortcut()); }

    // One dialog at a time: created on first use, then shown again.
    void show_settings() {
        if (!settings_) {
            SettingsHooks hooks;
            hooks.apply_shortcut = [this](const std::string& text) { return apply_shortcut(text); };
            // Search directories added/removed in Settings take effect without a restart.
            hooks.directories_changed = [this]() { start_rebuild(); };
            for (const auto& p : services_.engine->providers()) hooks.providers.emplace_back(p->id(), p->name());
            settings_ = std::make_unique<SettingsDialog>(config_, aliases_, theme_, std::move(hooks));
        }
        settings_->refresh_from_config();
        settings_->show();
        settings_->raise();
        settings_->activateWindow();
    }

    // Settings dialog applies a new shortcut: persist, re-grab, refresh labels.
    bool apply_shortcut(const std::string& text) {
        if (!config_.set_shortcut(text)) return false;
        tray_icon_.setToolTip("Nexus Launcher (" + shortcut_text() + ")");
        toggle_action_->setText("Toggle Launcher (" + shortcut_text() + ")");
        return shortcut_mgr_.apply_shortcut(text);
    }

    // Single-instance IPC server for instant toggle from shortcuts.
    void setup_ipc() {
        ipc_server_ = std::make_unique<QLocalServer>(&app_);
        QLocalServer::removeServer(IPC_SOCKET_NAME);  // Clean any previous stale socket file
        if (!ipc_server_->listen(IPC_SOCKET_NAME)) {
            NEXUS_LOG_WARN("Could not start IPC server: " + ipc_server_->errorString().toStdString());
            return;
        }
        NEXUS_LOG_INFO("Nexus IPC server listening on: " + std::string(IPC_SOCKET_NAME));
        QObject::connect(ipc_server_.get(), &QLocalServer::newConnection, [this]() {
            QLocalSocket* client = ipc_server_->nextPendingConnection();
            if (!client) return;
            QObject::connect(client, &QLocalSocket::readyRead, [this, client]() {
                const QString msg = QString::fromUtf8(client->readAll()).trimmed();
                if (msg == "TOGGLE") {
                    launcher_->toggle_launcher();
                } else if (msg == "SHOW") {
                    launcher_->show_launcher();
                } else if (msg == "HIDE") {
                    launcher_->hide_launcher();
                } else if (msg == "SETTINGS") {
                    show_settings();
                }
            });
        });
    }

    // Global hotkey manager (native X11 listener for X11 sessions).
    void setup_shortcut() {
        QObject::connect(&shortcut_mgr_, &GlobalShortcutManager::activated, [this]() { launcher_->toggle_launcher(); });
        QObject::connect(&shortcut_mgr_, &GlobalShortcutManager::grab_failed, [this](const QString& msg) {
            // On Wayland the single tray notice covers it; don't stack a second popup for the same cause.
            if (utils::is_wayland_session()) {
                NEXUS_LOG_WARN("Global shortcut grab failed: " + msg.toStdString());
                return;
            }
            QMessageBox::warning(nullptr, "Nexus global shortcut", msg + "\nChoose another one in Settings.");
        });
        shortcut_mgr_.apply_shortcut(config_.get_shortcut());
    }

    void setup_tray() {
        QIcon icon = QIcon::fromTheme("system-search");
        if (icon.isNull()) icon = QIcon::fromTheme("application-x-executable");
        tray_icon_.setIcon(icon);
        tray_icon_.setToolTip("Nexus Launcher (" + shortcut_text() + ")");

        toggle_action_ = tray_menu_.addAction("Toggle Launcher (" + shortcut_text() + ")");
        QObject::connect(toggle_action_, &QAction::triggered, [this]() { launcher_->toggle_launcher(); });
        QObject::connect(tray_menu_.addAction("Settings..."), &QAction::triggered, [this]() { show_settings(); });
        QObject::connect(tray_menu_.addAction("Rebuild Indexes"), &QAction::triggered, [this]() { start_rebuild(); });
        tray_menu_.addSeparator();
        QObject::connect(tray_menu_.addAction("Quit"), &QAction::triggered, [this]() {
            shortcut_mgr_.stop();
            app_.quit();
        });

        tray_icon_.setContextMenu(&tray_menu_);
        QObject::connect(&tray_icon_, &QSystemTrayIcon::activated, [this](QSystemTrayIcon::ActivationReason reason) {
            if (reason == QSystemTrayIcon::Trigger) launcher_->toggle_launcher();
        });

        const bool tray_available = QSystemTrayIcon::isSystemTrayAvailable();
        if (tray_available) tray_icon_.show();

        // Once per process start. Without a tray only the log line is shown.
        if (utils::is_wayland_session()) {
            const QString hint = "Global shortcuts don't work on Wayland. Bind `nexus --toggle` to a key in your compositor settings.";
            NEXUS_LOG_WARN(hint.toStdString());
            if (tray_available) tray_icon_.showMessage("Nexus", hint, QSystemTrayIcon::Warning, 10000);
        }
    }

    // One index build at a time (UI-thread state): a request during a build is remembered and run once it finishes,
    // so a build with an older directory list can never overwrite a newer one.
    void rebuild_finished() {
        rebuilding_ = false;
        if (std::exchange(rebuild_requested_, false)) start_rebuild();
    }

    // Rescan off the UI thread. Every rebuild is bracketed by arm()/resume() (see FileWatcher); builds are
    // serialised above, so arm/resume never overlap. A kernel event overflow also asks for a full rebuild.
    void start_rebuild() {
        if (rebuilding_) {
            rebuild_requested_ = true;
            return;
        }
        rebuilding_ = true;
        if (rebuild_thread_.joinable()) rebuild_thread_.join();  // previous worker already finished
        tray_icon_.showMessage("Nexus", "Rebuilding application and file indexes...", QSystemTrayIcon::Information, 3000);
        rebuild_thread_ = std::thread([this, options = app::file_index_options(config_)]() {
            const size_t count = services_.app_indexer->rebuild_index();
            file_watcher_.arm(options, quitting_);
            const size_t files = services_.file_indexer->rebuild(options, quitting_);
            file_watcher_.resume();
            QMetaObject::invokeMethod(&tray_icon_, [this, count, files]() {
                tray_icon_.showMessage("Nexus", QString("Indexes rebuilt (%1 apps, %2 files)").arg(count).arg(files),
                                       QSystemTrayIcon::Information, 3000);
                rebuild_finished();
            }, Qt::QueuedConnection);
        });
    }

    QApplication& app_;
    core::ConfigManager& config_;
    core::AliasManager& aliases_;
    app::Services services_;
    std::atomic<bool> quitting_{false};
    bool rebuilding_ = true;  // the startup build counts: until it lands, requests are queued
    bool rebuild_requested_ = false;
    index::FileWatcher file_watcher_;
    std::thread startup_index_thread_;
    std::thread rebuild_thread_;
    ThemeController theme_;
    std::unique_ptr<LauncherWindow> launcher_;
    std::unique_ptr<QLocalServer> ipc_server_;
    GlobalShortcutManager shortcut_mgr_;
    QSystemTrayIcon tray_icon_;
    QMenu tray_menu_;
    QAction* toggle_action_ = nullptr;
    std::unique_ptr<SettingsDialog> settings_;
};

} // namespace

int run_gui(QApplication& app, const std::vector<std::string>& args,
            core::ConfigManager& config, core::AliasManager& aliases) {
    const std::string arg1 = args.empty() ? "" : args[0];

    // Another instance already running: hand the command over and exit.
    const char* cmd = args.empty() ? "TOGGLE" : nullptr;
    if (arg1 == "--hide") cmd = "HIDE";
    else if (arg1 == "--show") cmd = "SHOW";
    else if (is_settings_arg(arg1)) cmd = "SETTINGS";
    else if (arg1 == "--toggle" || arg1 == "-t") cmd = "TOGGLE";
    if (cmd && send_to_running_instance(cmd)) return 0;

    // No instance and the user asked for settings: start one with the dialog open.
    GuiApp gui(app, config, aliases);
    return gui.run(is_settings_arg(arg1));
}

} // namespace nexus::ui
