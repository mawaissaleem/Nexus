#pragma once

#include <QObject>
#include "nexus/ui/portal_shortcut.hpp"
#include "nexus/utils/hotkey_backend.hpp"
#include <QString>
#include <atomic>
#include <string>
#include <thread>

namespace nexus::ui {

class GlobalShortcutManager : public QObject {
    Q_OBJECT

public:
    explicit GlobalShortcutManager(QObject* parent = nullptr);
    ~GlobalShortcutManager() override;

    bool start();
    void stop();

    // Re-grabs with a new "Ctrl+Alt+Space" style string, no restart needed.
    // Returns false if the string does not parse (previous grab is kept).
    // A grab the X server refuses is reported later through grab_failed().
    bool apply_shortcut(const std::string& shortcut);

    // Picked once at construction from the session type, the portal and the desktop.
    [[nodiscard]] utils::HotkeyBackend backend() const noexcept { return backend_; }

signals:
    void activated();
    void grab_failed(const QString& message);
    // The portal keeps an already-bound key; the user changes it in the system settings.
    void compositor_decides(const QString& message);

private:
    bool apply_portal_shortcut(const std::string& shortcut);
    void run_x11_listener();
    void run_windows_listener();

    utils::HotkeyBackend backend_;
    PortalShortcut portal_;
    bool portal_bound_{false};
    std::atomic<bool> running_{false};
    std::thread listener_thread_;
    std::string shortcut_{"Alt+Space"};
};

}  // namespace nexus::ui
