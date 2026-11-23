#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusObjectPath>

namespace nexus::ui {

// Client for org.freedesktop.portal.GlobalShortcuts: one session, one shortcut ("toggle").
class PortalShortcut : public QObject {
    Q_OBJECT

public:
    explicit PortalShortcut(QObject* parent = nullptr);

    // True when the desktop portal on the session bus offers GlobalShortcuts.
    static bool available();

    // Creates the session (first call) and offers `trigger` ("CTRL+ALT+space") as the preferred trigger.
    // Binding the same shortcut again does not change it: the compositor owns the key once bound.
    void bind(const QString& trigger);

signals:
    void activated();
    void failed(const QString& message);

private slots:
    void on_create_session_response(uint code, const QVariantMap& results);
    void on_bind_response(uint code, const QVariantMap& results);
    void on_activated(const QDBusObjectPath& session, const QString& shortcut_id, qulonglong timestamp,
                      const QVariantMap& options);

private:
    void send_bind_request();
    // Subscribes to the Request.Response signal for a new token, then returns the token to send with the call.
    QString expect_response(const char* slot);
    // Sends a portal call; a D-Bus error emits failed(), a request handle other than the expected one is followed.
    void send(const QDBusMessage& call, const QString& token, const char* slot);

    QString trigger_;
    QString session_;
    bool creating_{false};
    int token_counter_{0};
};

}  // namespace nexus::ui
