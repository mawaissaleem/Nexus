#pragma once

#include <QObject>
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

signals:
    void activated();
    void grab_failed(const QString& message);

private:
    void run_x11_listener();
    void run_windows_listener();

    std::atomic<bool> running_{false};
    std::thread listener_thread_;
    std::string shortcut_{"Alt+Space"};
};

} // namespace nexus::ui

