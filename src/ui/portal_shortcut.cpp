#include "nexus/ui/portal_shortcut.hpp"
#include "nexus/utils/logger.hpp"
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QList>

namespace nexus::ui {

namespace {

const QString kService = "org.freedesktop.portal.Desktop";
const QString kPath = "/org/freedesktop/portal/desktop";
const QString kInterface = "org.freedesktop.portal.GlobalShortcuts";
const QString kRequestInterface = "org.freedesktop.portal.Request";
const QString kShortcutId = "toggle";

// One entry of BindShortcuts' a(sa{sv}) argument.
struct ShortcutSpec {
    QString id;
    QVariantMap properties;
};

QDBusArgument& operator<<(QDBusArgument& arg, const ShortcutSpec& spec) {
    arg.beginStructure();
    arg << spec.id << spec.properties;
    arg.endStructure();
    return arg;
}

const QDBusArgument& operator>>(const QDBusArgument& arg, ShortcutSpec& spec) {
    arg.beginStructure();
    arg >> spec.id >> spec.properties;
    arg.endStructure();
    return arg;
}

// The portal answers on /request/<sender with ':' dropped and '.' as '_'>/<token>; subscribing first avoids a race.
QString request_path(const QString& token) {
    QString sender = QDBusConnection::sessionBus().baseService().mid(1);
    sender.replace('.', '_');
    return "/org/freedesktop/portal/desktop/request/" + sender + "/" + token;
}

// session_handle is documented as a string, but some portals send an object path.
QString session_from(const QVariantMap& results) {
    const QVariant value = results.value("session_handle");
    if (value.canConvert<QDBusObjectPath>()) return value.value<QDBusObjectPath>().path();
    return value.toString();
}

}  // namespace
}  // namespace nexus::ui

Q_DECLARE_METATYPE(nexus::ui::ShortcutSpec)
Q_DECLARE_METATYPE(QList<nexus::ui::ShortcutSpec>)

namespace nexus::ui {

PortalShortcut::PortalShortcut(QObject* parent) : QObject(parent) {
    qDBusRegisterMetaType<ShortcutSpec>();
    qDBusRegisterMetaType<QList<ShortcutSpec>>();
}

bool PortalShortcut::available() {
    QDBusMessage get = QDBusMessage::createMethodCall(kService, kPath, "org.freedesktop.DBus.Properties", "Get");
    get << kInterface << QString("version");
    // Short timeout: this runs at startup, and a stuck portal must not freeze Nexus for D-Bus's default 25 s.
    const QDBusMessage reply = QDBusConnection::sessionBus().call(get, QDBus::Block, 1000);
    return reply.type() == QDBusMessage::ReplyMessage;
}

QString PortalShortcut::expect_response(const char* slot) {
    const QString token = "nexus" + QString::number(token_counter_++);
    QDBusConnection::sessionBus().connect(kService, request_path(token), kRequestInterface, "Response", this, slot);
    return token;
}

void PortalShortcut::unsubscribe(const QString& path, const char* slot) {
    QDBusConnection::sessionBus().disconnect(kService, path, kRequestInterface, "Response", this, slot);
}

void PortalShortcut::unsubscribe_current_request(const char* slot) {
    if (!calledFromDBus()) return;
    unsubscribe(message().path(), slot);
}

void PortalShortcut::send(const QDBusMessage& call, const QString& token, const char* slot) {
    auto* watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, token, slot](QDBusPendingCallWatcher* w) {
        w->deleteLater();
        const QDBusPendingReply<QDBusObjectPath> reply = *w;
        // A refused call never sends a Response, so report it here or the shortcut silently stays unbound.
        if (reply.isError()) {
            unsubscribe(request_path(token), slot);
            creating_ = false;
            NEXUS_LOG_WARN("Portal call failed: " + reply.error().message().toStdString());
            emit failed("The desktop portal refused the Nexus shortcut. Set it in your system keyboard settings.");
            return;
        }
        // Old portals may answer on a different request path than the one built from the token.
        const QString handle = reply.value().path();
        if (handle != request_path(token)) {
            unsubscribe(request_path(token), slot);
            QDBusConnection::sessionBus().connect(kService, handle, kRequestInterface, "Response", this, slot);
        }
    });
}

void PortalShortcut::bind(const QString& trigger) {
    trigger_ = trigger;
    if (!session_.isEmpty()) {
        send_bind_request();
        return;
    }
    if (creating_) return;  // the pending response binds with the latest trigger_
    creating_ = true;

    const char* slot = SLOT(on_create_session_response(uint, QVariantMap));
    const QString token = expect_response(slot);
    QDBusConnection::sessionBus().connect(kService, kPath, kInterface, "Activated", this,
                                          SLOT(on_activated(QDBusObjectPath, QString, qulonglong, QVariantMap)));

    QVariantMap options;
    options["handle_token"] = token;
    options["session_handle_token"] = "nexus_session";
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, "CreateSession");
    call << options;
    send(call, token, slot);
}

void PortalShortcut::on_create_session_response(uint code, const QVariantMap& results) {
    unsubscribe_current_request(SLOT(on_create_session_response(uint, QVariantMap)));
    creating_ = false;
    session_ = session_from(results);
    if (code != 0 || session_.isEmpty()) {
        session_.clear();
        emit failed("The desktop portal refused to create a shortcut session.");
        return;
    }
    send_bind_request();
}

void PortalShortcut::send_bind_request() {
    const char* slot = SLOT(on_bind_response(uint, QVariantMap));
    const QString token = expect_response(slot);

    ShortcutSpec spec;
    spec.id = kShortcutId;
    spec.properties["description"] = "Toggle Nexus launcher";
    spec.properties["preferred_trigger"] = trigger_;

    QVariantMap options;
    options["handle_token"] = token;
    QDBusMessage call = QDBusMessage::createMethodCall(kService, kPath, kInterface, "BindShortcuts");
    call << QVariant::fromValue(QDBusObjectPath(session_)) << QVariant::fromValue(QList<ShortcutSpec>{spec})
         << QString() << options;
    send(call, token, slot);
}

void PortalShortcut::on_bind_response(uint code, const QVariantMap&) {
    unsubscribe_current_request(SLOT(on_bind_response(uint, QVariantMap)));
    // 1 = the user cancelled the compositor's prompt, 2 = failure.
    if (code != 0) emit failed("The desktop did not bind the Nexus shortcut. Set it in your system keyboard settings.");
}

void PortalShortcut::on_activated(const QDBusObjectPath& session, const QString& shortcut_id, qulonglong,
                                  const QVariantMap&) {
    if (session.path() == session_ && shortcut_id == kShortcutId) emit activated();
}

}  // namespace nexus::ui
