#include "nexus/ui/global_shortcut.hpp"
#include "nexus/utils/logger.hpp"
#include <QMetaObject>

#if defined(Q_OS_LINUX)
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <unistd.h>
#endif

#if defined(Q_OS_WIN)
#include <windows.h>
#endif

namespace nexus::ui {

GlobalShortcutManager::GlobalShortcutManager(QObject* parent)
    : QObject(parent) {}

GlobalShortcutManager::~GlobalShortcutManager() {
    stop();
}

bool GlobalShortcutManager::start() {
    if (running_.load()) return true;

    running_.store(true);

#if defined(Q_OS_LINUX)
    listener_thread_ = std::thread(&GlobalShortcutManager::run_x11_listener, this);
    return true;
#elif defined(Q_OS_WIN)
    listener_thread_ = std::thread(&GlobalShortcutManager::run_windows_listener, this);
    return true;
#else
    NEXUS_LOG_WARN("Global shortcuts not supported on this platform.");
    running_.store(false);
    return false;
#endif
}

void GlobalShortcutManager::stop() {
    if (!running_.load()) return;
    running_.store(false);

#if defined(Q_OS_LINUX)
    if (x11_display_) {
        Display* dpy = static_cast<Display*>(x11_display_);
        XEvent event{};
        event.type = ClientMessage;
        event.xclient.type = ClientMessage;
        event.xclient.display = dpy;
        event.xclient.window = DefaultRootWindow(dpy);
        event.xclient.message_type = XInternAtom(dpy, "NEXUS_QUIT", False);
        event.xclient.format = 32;
        XSendEvent(dpy, DefaultRootWindow(dpy), False, 0, &event);
        XFlush(dpy);
    }
#endif

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
}

#if defined(Q_OS_LINUX)
void GlobalShortcutManager::run_x11_listener() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        NEXUS_LOG_ERROR("Could not open X11 display for global shortcut listener");
        running_.store(false);
        return;
    }

    x11_display_ = display;
    Window root = DefaultRootWindow(display);
    KeyCode space_code = XKeysymToKeycode(display, XK_space);

    unsigned int modifiers[] = {
        Mod1Mask,                                  // Alt
        Mod1Mask | Mod2Mask,                       // Alt + NumLock
        Mod1Mask | LockMask,                       // Alt + CapsLock
        Mod1Mask | Mod2Mask | LockMask             // Alt + NumLock + CapsLock
    };

    for (unsigned int mod : modifiers) {
        XGrabKey(display, space_code, mod, root, True, GrabModeAsync, GrabModeAsync);
    }
    XSelectInput(display, root, KeyPressMask);
    XFlush(display);

    NEXUS_LOG_INFO("Global Alt+Space shortcut listener started (Linux/X11)");

    Atom quit_atom = XInternAtom(display, "NEXUS_QUIT", False);

    while (running_.load()) {
        XEvent ev{};
        XNextEvent(display, &ev);

        if (!running_.load()) break;

        if (ev.type == ClientMessage && ev.xclient.message_type == quit_atom) {
            break;
        }

        if (ev.type == KeyPress) {
            if (ev.xkey.keycode == space_code && (ev.xkey.state & Mod1Mask)) {
                NEXUS_LOG_DEBUG("Alt+Space triggered globally");
                QMetaObject::invokeMethod(this, "activated", Qt::QueuedConnection);
            }
        }
    }

    for (unsigned int mod : modifiers) {
        XUngrabKey(display, space_code, mod, root);
    }
    XCloseDisplay(display);
    x11_display_ = nullptr;
}
#endif

#if defined(Q_OS_WIN)
void GlobalShortcutManager::run_windows_listener() {
    const int HOTKEY_ID = 1001;
    if (!RegisterHotKey(NULL, HOTKEY_ID, MOD_ALT, VK_SPACE)) {
        NEXUS_LOG_ERROR("Failed to register Windows Alt+Space hotkey");
        running_.store(false);
        return;
    }

    NEXUS_LOG_INFO("Global Alt+Space shortcut listener started (Windows)");

    MSG msg;
    while (running_.load() && GetMessage(&msg, NULL, 0, 0)) {
        if (msg.message == WM_HOTKEY && msg.wParam == HOTKEY_ID) {
            NEXUS_LOG_DEBUG("Alt+Space triggered on Windows");
            QMetaObject::invokeMethod(this, "activated", Qt::QueuedConnection);
        }
    }

    UnregisterHotKey(NULL, HOTKEY_ID);
}
#endif

} // namespace nexus::ui

