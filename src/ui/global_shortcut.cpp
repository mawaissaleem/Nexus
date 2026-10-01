#include "nexus/ui/global_shortcut.hpp"
#include "nexus/utils/logger.hpp"
#include "nexus/utils/shortcut.hpp"
#include <cctype>
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
    if (listener_thread_.joinable()) listener_thread_.join();  // listener exited on its own (grab failed)

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
    running_.store(false);

#if defined(Q_OS_LINUX)
    if (listener_thread_.joinable()) {
        // Wake the listener's blocking XNextEvent from a private connection:
        // Xlib connections must not be shared across threads. KeyPressMask makes
        // the server deliver it to the listener (the only client selecting it on root).
        if (Display* dpy = XOpenDisplay(nullptr)) {
            XEvent event{};
            event.type = ClientMessage;
            event.xclient.type = ClientMessage;
            event.xclient.display = dpy;
            event.xclient.window = DefaultRootWindow(dpy);
            event.xclient.message_type = XInternAtom(dpy, "NEXUS_QUIT", False);
            event.xclient.format = 32;
            XSendEvent(dpy, DefaultRootWindow(dpy), False, KeyPressMask, &event);
            XCloseDisplay(dpy);
        }
    }
#endif

    if (listener_thread_.joinable()) {
        listener_thread_.join();
    }
}

bool GlobalShortcutManager::apply_shortcut(const std::string& shortcut) {
    if (!utils::parse_shortcut(shortcut)) return false;
    stop();
    shortcut_ = shortcut;
    return start();
}

#if defined(Q_OS_LINUX)
namespace {
// XGrabKey errors (BadAccess when another client owns the key) arrive asynchronously
// through this process-wide handler; XSync after grabbing makes them arrive before we look.
std::atomic<bool> g_x_error{false};
int on_x_error(Display*, XErrorEvent*) {
    g_x_error.store(true);
    return 0;
}
} // namespace

void GlobalShortcutManager::run_x11_listener() {
    Display* display = XOpenDisplay(nullptr);
    if (!display) {
        NEXUS_LOG_ERROR("Could not open X11 display for global shortcut listener");
        running_.store(false);
        return;
    }

    auto parsed = utils::parse_shortcut(shortcut_);
    Window root = DefaultRootWindow(display);

    // The key name may be typed in any case; X keysym names are case-sensitive ("space", "Return", "F1").
    KeySym sym = NoSymbol;
    if (parsed) {
        std::string lower = parsed->key, cap;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        cap = lower;
        cap[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(cap[0])));
        for (const std::string& name : {parsed->key, lower, cap}) {
            sym = XStringToKeysym(name.c_str());
            if (sym != NoSymbol) break;
        }
    }
    KeyCode code = sym == NoSymbol ? 0 : XKeysymToKeycode(display, sym);
    if (code == 0) {
        std::string msg = "Unknown key in shortcut '" + shortcut_ + "'";
        NEXUS_LOG_ERROR(msg);
        QMetaObject::invokeMethod(this, "grab_failed", Qt::QueuedConnection, Q_ARG(QString, QString::fromStdString(msg)));
        running_.store(false);
        XCloseDisplay(display);
        return;
    }

    unsigned int base = (parsed->ctrl ? ControlMask : 0) | (parsed->alt ? Mod1Mask : 0) |
                        (parsed->shift ? ShiftMask : 0) | (parsed->super ? Mod4Mask : 0);
    const unsigned int relevant = ControlMask | Mod1Mask | ShiftMask | Mod4Mask;

    // NumLock (Mod2) and CapsLock (Lock) change the event state, so grab every combination.
    unsigned int modifiers[] = {base, base | Mod2Mask, base | LockMask, base | Mod2Mask | LockMask};

    g_x_error.store(false);
    XErrorHandler old_handler = XSetErrorHandler(on_x_error);
    for (unsigned int mod : modifiers) {
        XGrabKey(display, code, mod, root, True, GrabModeAsync, GrabModeAsync);
    }
    XSync(display, False);
    XSetErrorHandler(old_handler);

    if (g_x_error.load()) {
        for (unsigned int mod : modifiers) XUngrabKey(display, code, mod, root);
        std::string msg = "Could not grab global shortcut '" + shortcut_ + "': another application already uses it.";
        NEXUS_LOG_ERROR(msg);
        QMetaObject::invokeMethod(this, "grab_failed", Qt::QueuedConnection, Q_ARG(QString, QString::fromStdString(msg)));
        running_.store(false);
        XCloseDisplay(display);
        return;
    }

    XSelectInput(display, root, KeyPressMask);
    XFlush(display);

    NEXUS_LOG_INFO("Global " + shortcut_ + " shortcut listener started (Linux/X11)");

    Atom quit_atom = XInternAtom(display, "NEXUS_QUIT", False);

    while (running_.load()) {
        XEvent ev{};
        XNextEvent(display, &ev);

        if (!running_.load()) break;

        if (ev.type == ClientMessage && ev.xclient.message_type == quit_atom) {
            break;
        }

        if (ev.type == KeyPress) {
            if (ev.xkey.keycode == code && (ev.xkey.state & relevant) == base) {
                NEXUS_LOG_DEBUG(shortcut_ + " triggered globally");
                QMetaObject::invokeMethod(this, "activated", Qt::QueuedConnection);
            }
        }
    }

    for (unsigned int mod : modifiers) {
        XUngrabKey(display, code, mod, root);
    }
    XCloseDisplay(display);
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

