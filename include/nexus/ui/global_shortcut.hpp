#pragma once

#include <QObject>
#include <atomic>
#include <thread>

namespace nexus::ui {

class GlobalShortcutManager : public QObject {
    Q_OBJECT

public:
    explicit GlobalShortcutManager(QObject* parent = nullptr);
    ~GlobalShortcutManager() override;

    bool start();
    void stop();

signals:
    void activated();

private:
    void run_x11_listener();
    void run_windows_listener();

    std::atomic<bool> running_{false};
    std::thread listener_thread_;

#if defined(Q_OS_LINUX)
    void* x11_display_{nullptr};
#endif
};

} // namespace nexus::ui

