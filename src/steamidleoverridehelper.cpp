// Strict replication of DeckyInhibitScreenSaver's runtime mechanism, in-process,
// without depending on Decky Loader.
//
// Layer 1: register the DBus services Steam Game Mode does NOT provide:
//          org.freedesktop.ScreenSaver       (paths /ScreenSaver and /org/freedesktop/ScreenSaver)
//          org.freedesktop.PowerManagement   (path /org/freedesktop/PowerManagement/Inhibit)
//          org.gnome.SessionManager          (path /org/gnome/SessionManager)
//          Senders whose `application` field is exactly "Steam" or "./steamwebhelper" are ignored,
//          matching Decky's `ignore_application` list.
//
// Layer 2: when at least one inhibit is active, connect to steamwebhelper's CEF DevTools
//          debugger on steamwebhelper's `--remote-debugging-port` (8080 fallback), find the
//          `SharedJSContext` target, and call `SteamClient.System.UpdateSettings(...)` /
//          `SteamClient.Settings.SetSetting(...)` with the same protobuf-encoded payload that
//          Decky's `genSettings` produces (battery_idle, ac_idle, battery_suspend, ac_suspend).
//          On the last UnInhibit, restore the user's configured Steam idle values.
//
// Also keeps the existing local-socket protocol so aerocore-browser itself can drive the same
// state machine via the PowerInhibitor's "steam-idle-helper" backend (synthetic inhibit cookie).

#include "steamidleoverridehelper.h"
#include "steamidleoverrideadaptors.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusServiceWatcher>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRegularExpression>
#include <QSaveFile>
#include <QTimer>
#include <QUrl>
#include <QWebSocket>
#include <QtGlobal>

#include <csignal>
#include <functional>
#include <signal.h>

namespace {
volatile std::sig_atomic_t gHelperTerminationSignal = 0;

extern "C" void steamIdleHelperTerminationSignalHandler(int signalNumber)
{
    gHelperTerminationSignal = signalNumber;
}

void installTerminationHandlers()
{
    std::signal(SIGINT, steamIdleHelperTerminationSignalHandler);
    std::signal(SIGTERM, steamIdleHelperTerminationSignalHandler);
    std::signal(SIGHUP, steamIdleHelperTerminationSignalHandler);
}

constexpr int kOwnerPollMs = 2000;
constexpr int kLocalIoTimeoutMs = 1000;
constexpr int kDefaultCdpHttpPort = 8080;
const QString kCdpHost = QStringLiteral("127.0.0.1");

// Fallback restore defaults (seconds): battery_idle, ac_idle, battery_suspend, ac_suspend.
constexpr int kRestoreBatteryIdle = 300;
constexpr int kRestoreAcIdle = 300;
constexpr int kRestoreBatterySuspend = 600;
constexpr int kRestoreAcSuspend = 600;

SteamIdleSettings fallbackSteamIdleSettings()
{
    SteamIdleSettings settings;
    settings.batteryIdle = kRestoreBatteryIdle;
    settings.acIdle = kRestoreAcIdle;
    settings.batterySuspend = kRestoreBatterySuspend;
    settings.acSuspend = kRestoreAcSuspend;
    return settings;
}

bool isProcessAlive(qint64 pid)
{
    if (pid <= 0)
        return false;
    return ::kill(static_cast<pid_t>(pid), 0) == 0 || errno == EPERM;
}

QString decodeReason(const QByteArray &payload)
{
    return QString::fromUtf8(QByteArray::fromBase64(payload, QByteArray::Base64UrlEncoding));
}

int remoteDebuggingPortFromArgs(const QList<QByteArray> &args)
{
    for (int i = 0; i < args.size(); ++i) {
        const QByteArray arg = args.at(i);
        constexpr auto prefix = "--remote-debugging-port=";
        if (arg.startsWith(prefix)) {
            bool ok = false;
            const int port = arg.mid(std::char_traits<char>::length(prefix)).toInt(&ok);
            if (ok && port > 0 && port <= 65535)
                return port;
        }
        if (arg == "--remote-debugging-port" && i + 1 < args.size()) {
            bool ok = false;
            const int port = args.at(i + 1).toInt(&ok);
            if (ok && port > 0 && port <= 65535)
                return port;
        }
    }
    return 0;
}

int steamWebHelperRemoteDebuggingPort()
{
    QDir proc(QStringLiteral("/proc"));
    const QStringList entries = proc.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        bool pidOk = false;
        entry.toLongLong(&pidOk);
        if (!pidOk)
            continue;

        QFile cmdline(proc.filePath(entry + QStringLiteral("/cmdline")));
        if (!cmdline.open(QIODevice::ReadOnly))
            continue;
        const QByteArray data = cmdline.readAll();
        if (!data.contains("steamwebhelper"))
            continue;

        QList<QByteArray> args;
        for (const QByteArray &part : data.split('\0')) {
            if (!part.isEmpty())
                args.append(part);
        }
        const int port = remoteDebuggingPortFromArgs(args);
        if (port > 0)
            return port;
    }

    return kDefaultCdpHttpPort;
}

QString steamConfigPath()
{
    return QDir::home().filePath(QStringLiteral(".local/share/Steam/config/config.vdf"));
}

int readSteamIdleValue(const QString &text, const QString &key, int fallback)
{
    const QRegularExpression pattern(QStringLiteral("(\\\"") + QRegularExpression::escape(key)
                                     + QStringLiteral("\\\"\\s*\\\")([^\\\"]*)(\\\")"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return fallback;

    bool ok = false;
    const int value = match.captured(2).trimmed().toInt(&ok);
    return ok && value >= 0 ? value : fallback;
}

SteamIdleSettings readSteamIdleSettings()
{
    SteamIdleSettings settings = fallbackSteamIdleSettings();
    QFile file(steamConfigPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning().noquote() << QStringLiteral("Steam idle helper could not read %1; using fallback restore values")
                                    .arg(QFileInfo(file).absoluteFilePath());
        return settings;
    }

    const QString text = QString::fromUtf8(file.readAll());
    settings.batteryIdle = readSteamIdleValue(text, QStringLiteral("IdleBacklightDimBatterySeconds"), settings.batteryIdle);
    settings.acIdle = readSteamIdleValue(text, QStringLiteral("IdleBacklightDimACSeconds"), settings.acIdle);
    settings.batterySuspend = readSteamIdleValue(text, QStringLiteral("IdleSuspendBatterySeconds"), settings.batterySuspend);
    settings.acSuspend = readSteamIdleValue(text, QStringLiteral("IdleSuspendACSeconds"), settings.acSuspend);
    return settings;
}

bool updateSteamIdleValue(QString *text, const QString &key, int value)
{
    const QRegularExpression pattern(QStringLiteral("(\\\"") + QRegularExpression::escape(key)
                                     + QStringLiteral("\\\"\\s*\\\")([^\\\"]*)(\\\")"));
    const QRegularExpressionMatch match = pattern.match(*text);
    if (!match.hasMatch())
        return false;

    text->replace(match.capturedStart(2), match.capturedLength(2), QString::number(value));
    return true;
}

bool writeSteamIdleSettings(const SteamIdleSettings &settings)
{
    QFile file(steamConfigPath());
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning().noquote() << QStringLiteral("Steam idle helper could not read %1 for writeback")
                                    .arg(QFileInfo(file).absoluteFilePath());
        return false;
    }

    QString text = QString::fromUtf8(file.readAll());
    file.close();

    const bool ok = updateSteamIdleValue(&text, QStringLiteral("IdleBacklightDimBatterySeconds"), settings.batteryIdle)
        && updateSteamIdleValue(&text, QStringLiteral("IdleBacklightDimACSeconds"), settings.acIdle)
        && updateSteamIdleValue(&text, QStringLiteral("IdleSuspendBatterySeconds"), settings.batterySuspend)
        && updateSteamIdleValue(&text, QStringLiteral("IdleSuspendACSeconds"), settings.acSuspend);
    if (!ok) {
        qWarning().noquote() << QStringLiteral("Steam idle helper could not update all idle keys in config.vdf");
        return false;
    }

    QSaveFile outFile(steamConfigPath());
    if (!outFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        qWarning().noquote() << QStringLiteral("Steam idle helper could not open %1 for writeback")
                                    .arg(QFileInfo(outFile).absoluteFilePath());
        return false;
    }
    outFile.write(text.toUtf8());
    if (!outFile.commit()) {
        qWarning().noquote() << QStringLiteral("Steam idle helper failed to commit config.vdf writeback");
        return false;
    }
    return true;
}

bool areAllSteamIdleValuesDisabled(const SteamIdleSettings &settings)
{
    return settings.batteryIdle == 0
        && settings.acIdle == 0
        && settings.batterySuspend == 0
        && settings.acSuspend == 0;
}

// JS that runs inside SharedJSContext. Builds the same protobuf bytes as Decky's
// `genSettings` (field<<3 | wireType, then either varint int32 or little-endian float32),
// concatenates the four fields, and dispatches to UpdateSettings / SetSetting per the
// SteamClient023 detection. Returns "ok" / "err:<msg>".
QString buildSteamUpdateJs(int batteryIdle, int acIdle, int batterySuspend, int acSuspend)
{
    QString tpl = QStringLiteral(R"JS((async () => {
  try {
    const enc = (field, wire, value) => {
      const buf = [];
      let key = (field << 3) | wire;
      do { let b = key & 0x7F; key >>>= 7; if (key) b |= 0x80; buf.push(b); } while (key);
      if (wire === 0) {
        let v = value >>> 0;
        do { let b = v & 0x7F; v >>>= 7; if (v) b |= 0x80; buf.push(b); } while (v);
      } else if (wire === 5) {
        const vb = new Uint8Array(new Float32Array([value]).buffer);
        for (const x of vb) buf.push(x);
      } else { throw new Error('wire'); }
      return String.fromCharCode.apply(null, buf);
    };
    if (typeof SteamClient === 'undefined' || !SteamClient.System || !SteamClient.System.UpdateSettings) {
      return 'err:no-steamclient';
    }
    const hasNewSuspend = !SteamClient.System.RegisterForOnSuspendRequest;
    const idleData = enc(1, 5, %1) + enc(2, 5, %2);
    let suspendData;
    if (hasNewSuspend) {
      suspendData = enc(24003, 0, %3) + enc(24004, 0, %4);
    } else {
      suspendData = enc(3, 5, %3) + enc(4, 5, %4);
    }
    await SteamClient.System.UpdateSettings(window.btoa(idleData));
    if (hasNewSuspend) {
      if (!SteamClient.Settings || !SteamClient.Settings.SetSetting) return 'err:no-setsetting';
      await SteamClient.Settings.SetSetting(window.btoa(suspendData));
    } else {
      await SteamClient.System.UpdateSettings(window.btoa(suspendData));
    }
    return 'ok';
  } catch (e) {
    return 'err:' + (e && e.message ? e.message : String(e));
  }
})())JS");
    return tpl.arg(batteryIdle).arg(acIdle).arg(batterySuspend).arg(acSuspend);
}

QString buildSteamSnapshotJs()
{
        return QStringLiteral(R"JS((async () => {
    try {
        if (typeof SteamClient === 'undefined' || !SteamClient.System || !SteamClient.System.RegisterForSettingsChanges) {
            return { ok: false, error: 'err:no-register' };
        }

        const decode = (payload) => {
            const bytes = payload instanceof Uint8Array
                ? payload
                : payload instanceof ArrayBuffer
                    ? new Uint8Array(payload)
                    : payload && payload.buffer instanceof ArrayBuffer
                        ? new Uint8Array(payload.buffer, payload.byteOffset || 0, payload.byteLength || payload.buffer.byteLength)
                        : null;
            if (!bytes) throw new Error('bad-payload');

            const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
            let offset = 0;
            const out = {
                batteryIdle: 0,
                acIdle: 0,
                batterySuspend: 0,
                acSuspend: 0,
            };

            const readVarint = () => {
                let shift = 0;
                let value = 0;
                while (offset < bytes.length) {
                    const b = bytes[offset++];
                    value |= (b & 0x7f) << shift;
                    if (!(b & 0x80)) return value >>> 0;
                    shift += 7;
                    if (shift > 28) throw new Error('varint-overflow');
                }
                throw new Error('varint-truncated');
            };

            while (offset < bytes.length) {
                const key = readVarint();
                const field = key >>> 3;
                const wire = key & 0x7;

                if (wire === 0) {
                    const value = readVarint();
                    if (field === 24003) out.batterySuspend = value;
                    else if (field === 24004) out.acSuspend = value;
                } else if (wire === 5) {
                    if (offset + 4 > bytes.length) throw new Error('fixed32-truncated');
                    const value = Math.round(view.getFloat32(offset, true));
                    if (field === 1) out.batteryIdle = value;
                    else if (field === 2) out.acIdle = value;
                    else if (field === 3) out.batterySuspend = value;
                    else if (field === 4) out.acSuspend = value;
                    offset += 4;
                } else if (wire === 1) {
                    offset += 8;
                } else if (wire === 2) {
                    const len = readVarint();
                    offset += len;
                } else {
                    throw new Error('unsupported-wire:' + wire);
                }

                if (offset > bytes.length) throw new Error('field-overrun');
            }

            return out;
        };

        return await new Promise((resolve) => {
            let done = false;
            let handle = null;
            const finish = (result) => {
                if (done) return;
                done = true;
                try {
                    if (handle && typeof handle.unregister === 'function') handle.unregister();
                } catch (_) {
                }
                resolve(result);
            };

            handle = SteamClient.System.RegisterForSettingsChanges((payload) => {
                try {
                    const decoded = decode(payload);
                    finish({ ok: true, ...decoded });
                } catch (e) {
                    finish({ ok: false, error: e && e.message ? e.message : String(e) });
                }
            });

            setTimeout(() => finish({ ok: false, error: 'timeout' }), 1000);
        });
    } catch (e) {
        return { ok: false, error: e && e.message ? e.message : String(e) };
    }
})())JS");
}
} // namespace

// ---------------------------------------------------------------------------
// SteamFrontendBridge: discover SharedJSContext over CDP HTTP, open a WebSocket
// to it, and execute updateSetting JS via Runtime.evaluate.
// ---------------------------------------------------------------------------
class SteamFrontendBridge : public QObject
{
    Q_OBJECT
public:
    explicit SteamFrontendBridge(QObject *parent = nullptr) : QObject(parent)
    {
        m_net = new QNetworkAccessManager(this);
        m_socket = new QWebSocket(QStringLiteral("http://127.0.0.1"),
                                  QWebSocketProtocol::VersionLatest, this);
        connect(m_socket, &QWebSocket::connected, this, &SteamFrontendBridge::onConnected);
        connect(m_socket, &QWebSocket::textMessageReceived, this, &SteamFrontendBridge::onTextMessage);
        connect(m_socket, &QWebSocket::disconnected, this, &SteamFrontendBridge::onDisconnected);
        connect(m_socket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
                this, &SteamFrontendBridge::onSocketError);
    }

    // Request to push (b_idle, ac_idle, b_susp, ac_susp). Coalesces concurrent calls.
    void updateSettings(int batteryIdle, int acIdle, int batterySuspend, int acSuspend,
                        const QString &reason)
    {
        m_pending = QVector<int>{batteryIdle, acIdle, batterySuspend, acSuspend};
        m_pendingReason = reason;
        m_havePending = true;
        emitIdleIfSettled();
        ensureConnected();
        flushPending();
    }

    void captureCurrentSettings(const QString &reason,
                                std::function<void(bool, const SteamIdleSettings &)> callback)
    {
        m_snapshotPending = true;
        m_snapshotPendingReason = reason;
        m_snapshotPendingCallback = std::move(callback);
        emitIdleIfSettled();
        ensureConnected();
        flushPending();
    }

    bool isIdle() const
    {
        return !m_havePending && !m_snapshotPending && m_inFlightKind == InFlightKind::None;
    }

signals:
    void idleStateChanged();

private slots:
    void onConnected()
    {
        qInfo().noquote() << QStringLiteral("Steam idle bridge connected to %1").arg(m_targetWs);
        flushPending();
    }

    void onDisconnected()
    {
        qInfo().noquote() << QStringLiteral("Steam idle bridge disconnected from SharedJSContext");
        m_targetWs.clear();
        if (m_inFlightKind == InFlightKind::Update) {
            m_havePending = true;
        } else if (m_inFlightKind == InFlightKind::Snapshot) {
            m_snapshotPending = true;
            m_snapshotPendingReason = m_inFlightReason;
            m_snapshotPendingCallback = std::move(m_inFlightSnapshotCallback);
        }
        m_inFlightKind = InFlightKind::None;
        m_inFlightReason.clear();
        if (m_havePending || m_snapshotPending)
            scheduleReconnect();
    }

    void onSocketError(QAbstractSocket::SocketError)
    {
        qWarning().noquote() << QStringLiteral("Steam idle bridge socket error: %1")
                                    .arg(m_socket->errorString());
    }

    void onTextMessage(const QString &text)
    {
        const QJsonDocument doc = QJsonDocument::fromJson(text.toUtf8());
        if (!doc.isObject())
            return;
        const QJsonObject obj = doc.object();
        if (!obj.contains(QStringLiteral("id")))
            return;
        const int id = obj.value(QStringLiteral("id")).toInt();
        const QJsonObject result = obj.value(QStringLiteral("result")).toObject();
        const QJsonObject resultVal = result.value(QStringLiteral("result")).toObject();
        const QJsonValue returnedValue = resultVal.value(QStringLiteral("value"));
        const QJsonObject err = obj.value(QStringLiteral("error")).toObject();
        const InFlightKind kind = m_inFlightKind;
        const QString reason = m_inFlightReason;
        auto snapshotCallback = std::move(m_inFlightSnapshotCallback);
        m_inFlightKind = InFlightKind::None;
        m_inFlightReason.clear();
        if (!err.isEmpty()) {
            qWarning().noquote() << QStringLiteral("Steam idle bridge CDP error id=%1 msg=%2")
                                        .arg(id)
                                        .arg(err.value(QStringLiteral("message")).toString());
            if (kind == InFlightKind::Snapshot && snapshotCallback) {
                snapshotCallback(false, fallbackSteamIdleSettings());
            }
        } else {
            if (kind == InFlightKind::Snapshot) {
                const QJsonObject snapshot = returnedValue.toObject();
                SteamIdleSettings settings = fallbackSteamIdleSettings();
                const bool ok = snapshot.value(QStringLiteral("ok")).toBool(false);
                if (ok) {
                    settings.batteryIdle = snapshot.value(QStringLiteral("batteryIdle")).toInt(settings.batteryIdle);
                    settings.acIdle = snapshot.value(QStringLiteral("acIdle")).toInt(settings.acIdle);
                    settings.batterySuspend = snapshot.value(QStringLiteral("batterySuspend")).toInt(settings.batterySuspend);
                    settings.acSuspend = snapshot.value(QStringLiteral("acSuspend")).toInt(settings.acSuspend);
                    qInfo().noquote() << QStringLiteral("Steam idle bridge snapshot id=%1 reason=%2 b_idle=%3 ac_idle=%4 b_susp=%5 ac_susp=%6")
                                             .arg(id)
                                             .arg(reason)
                                             .arg(settings.batteryIdle)
                                             .arg(settings.acIdle)
                                             .arg(settings.batterySuspend)
                                             .arg(settings.acSuspend);
                } else {
                    qWarning().noquote() << QStringLiteral("Steam idle bridge snapshot failed id=%1 reason=%2 error=%3")
                                                .arg(id)
                                                .arg(reason)
                                                .arg(snapshot.value(QStringLiteral("error")).toString());
                }
                if (snapshotCallback) {
                    snapshotCallback(ok, settings);
                }
            } else {
                qInfo().noquote() << QStringLiteral("Steam idle bridge applied id=%1 reason=%2 result=%3")
                                         .arg(id)
                                         .arg(reason)
                                         .arg(returnedValue.toString());
            }
        }
        emitIdleIfSettled();
        if (m_snapshotPending || m_havePending)
            flushPending();
    }

    void onTargetLookup()
    {
        const int cdpPort = steamWebHelperRemoteDebuggingPort();
        QNetworkRequest req(QUrl(QStringLiteral("http://%1:%2/json").arg(kCdpHost).arg(cdpPort)));
        QNetworkReply *reply = m_net->get(req);
        connect(reply, &QNetworkReply::finished, this, [this, reply]() {
            reply->deleteLater();
            if (reply->error() != QNetworkReply::NoError) {
                qWarning().noquote() << QStringLiteral("Steam idle bridge target lookup failed: %1")
                                            .arg(reply->errorString());
                scheduleReconnect();
                return;
            }
            const QJsonDocument doc = QJsonDocument::fromJson(reply->readAll());
            if (!doc.isArray()) {
                qWarning().noquote() << QStringLiteral("Steam idle bridge target lookup: not an array");
                scheduleReconnect();
                return;
            }
            QString ws;
            for (const QJsonValue &v : doc.array()) {
                const QJsonObject o = v.toObject();
                if (o.value(QStringLiteral("title")).toString() == QStringLiteral("SharedJSContext")) {
                    ws = o.value(QStringLiteral("webSocketDebuggerUrl")).toString();
                    break;
                }
            }
            if (ws.isEmpty()) {
                qWarning().noquote() << QStringLiteral("Steam idle bridge: SharedJSContext target not found");
                scheduleReconnect();
                return;
            }
            m_targetWs = ws;
            m_socket->open(QUrl(ws));
        });
    }

    void scheduleReconnect()
    {
        if (m_reconnectTimer && m_reconnectTimer->isActive())
            return;
        if (!m_reconnectTimer) {
            m_reconnectTimer = new QTimer(this);
            m_reconnectTimer->setSingleShot(true);
            connect(m_reconnectTimer, &QTimer::timeout, this, &SteamFrontendBridge::ensureConnected);
        }
        m_reconnectTimer->start(2000);
    }

    void ensureConnected()
    {
        if (m_socket->state() == QAbstractSocket::ConnectedState)
            return;
        if (m_socket->state() == QAbstractSocket::ConnectingState)
            return;
        onTargetLookup();
    }

    void flushPending()
    {
        if (m_socket->state() != QAbstractSocket::ConnectedState)
            return;
        if (m_inFlightKind != InFlightKind::None)
            return;

        QString js;
        QString logLine;
        QJsonObject params;
        QJsonObject msg;
        const int id = ++m_nextId;
        msg.insert(QStringLiteral("id"), id);
        msg.insert(QStringLiteral("method"), QStringLiteral("Runtime.evaluate"));

        if (m_snapshotPending) {
            js = buildSteamSnapshotJs();
            m_inFlightKind = InFlightKind::Snapshot;
            m_inFlightReason = m_snapshotPendingReason;
            m_inFlightSnapshotCallback = std::move(m_snapshotPendingCallback);
            m_snapshotPending = false;
            params.insert(QStringLiteral("returnByValue"), true);
            logLine = QStringLiteral("Steam idle bridge requesting runtime snapshot reason=%1")
                              .arg(m_inFlightReason);
        } else if (m_havePending) {
            js = buildSteamUpdateJs(m_pending[0], m_pending[1], m_pending[2], m_pending[3]);
            m_inFlightKind = InFlightKind::Update;
            m_inFlightReason = m_pendingReason;
            m_havePending = false;
            params.insert(QStringLiteral("returnByValue"), true);
            logLine = QStringLiteral("Steam idle bridge -> b_idle=%1 ac_idle=%2 b_susp=%3 ac_susp=%4 reason=%5")
                              .arg(m_pending[0]).arg(m_pending[1]).arg(m_pending[2]).arg(m_pending[3])
                              .arg(m_inFlightReason);
        } else {
            return;
        }

        params.insert(QStringLiteral("expression"), js);
        params.insert(QStringLiteral("awaitPromise"), true);
        msg.insert(QStringLiteral("params"), params);
        const QByteArray payload = QJsonDocument(msg).toJson(QJsonDocument::Compact);
        m_socket->sendTextMessage(QString::fromUtf8(payload));
        qInfo().noquote() << logLine;
    }

    void emitIdleIfSettled()
    {
        if (isIdle())
            emit idleStateChanged();
    }

private:
    enum class InFlightKind {
        None,
        Update,
        Snapshot,
    };

    QNetworkAccessManager *m_net = nullptr;
    QWebSocket *m_socket = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QString m_targetWs;
    int m_nextId = 0;
    InFlightKind m_inFlightKind = InFlightKind::None;
    QString m_inFlightReason;
    std::function<void(bool, const SteamIdleSettings &)> m_inFlightSnapshotCallback;
    bool m_havePending = false;
    QVector<int> m_pending;
    QString m_pendingReason;
    bool m_snapshotPending = false;
    QString m_snapshotPendingReason;
    std::function<void(bool, const SteamIdleSettings &)> m_snapshotPendingCallback;
};

// ---------------------------------------------------------------------------
// InhibitBroker
// ---------------------------------------------------------------------------
InhibitBroker::InhibitBroker(const QString &localServerName,
                             qint64 ownerPid,
                             SteamFrontendBridge *bridge,
                             QObject *parent)
    : QObject(parent)
    , m_localServerName(localServerName)
    , m_ownerPid(ownerPid)
    , m_bridge(bridge)
{
}

bool InhibitBroker::start()
{
    registerDBus();

    m_server = new QLocalServer(this);
    QLocalServer::removeServer(m_localServerName);
    if (!m_server->listen(m_localServerName)) {
        qWarning().noquote() << QStringLiteral("Steam idle helper failed to listen on %1: %2")
                                    .arg(m_localServerName)
                                    .arg(m_server->errorString());
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, &InhibitBroker::onLocalConnection);

    m_ownerTimer = new QTimer(this);
    m_ownerTimer->setInterval(kOwnerPollMs);
    connect(m_ownerTimer, &QTimer::timeout, this, &InhibitBroker::onOwnerPoll);
    m_ownerTimer->start();

    qInfo().noquote() << QStringLiteral("Steam idle helper listening on %1 owner pid=%2 dbus=%3")
                             .arg(m_localServerName)
                             .arg(m_ownerPid)
                             .arg(m_dbusOk ? QStringLiteral("ok") : QStringLiteral("failed"));
    // Sentinel for the parent client to detect the listening state.
    qInfo().noquote() << QStringLiteral("LISTENING");
    return true;
}

void InhibitBroker::registerDBus()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        qWarning().noquote() << QStringLiteral("Steam idle helper: session bus not available: %1")
                                    .arg(bus.lastError().message());
        return;
    }

    auto *screensaver = new ScreenSaverInhibitObject(this, this);
    auto *power = new PowerManagementInhibitObject(this, this);
    auto *gnome = new GnomeSessionManagerObject(this, this);
    const auto opts = QDBusConnection::ExportScriptableSlots
                    | QDBusConnection::ExportScriptableInvokables
                    | QDBusConnection::ExportScriptableProperties
                    | QDBusConnection::ExportAllSignals;

    auto tryRegisterObject = [&](const QString &path, QObject *obj) {
        if (!bus.registerObject(path, obj, opts)) {
            qWarning().noquote() << QStringLiteral("Steam idle helper failed to register object at %1: %2")
                                        .arg(path)
                                        .arg(bus.lastError().message());
            return false;
        }
        return true;
    };

    bool any = false;
    any |= tryRegisterObject(QStringLiteral("/ScreenSaver"), screensaver);
    any |= tryRegisterObject(QStringLiteral("/org/freedesktop/ScreenSaver"), screensaver);
    any |= tryRegisterObject(QStringLiteral("/org/freedesktop/PowerManagement/Inhibit"), power);
    any |= tryRegisterObject(QStringLiteral("/org/gnome/SessionManager"), gnome);

    auto tryRegisterService = [&](const QString &name) {
        if (!bus.registerService(name)) {
            qWarning().noquote() << QStringLiteral("Steam idle helper failed to acquire bus name %1: %2")
                                        .arg(name)
                                        .arg(bus.lastError().message());
            return false;
        }
        qInfo().noquote() << QStringLiteral("Steam idle helper acquired bus name %1").arg(name);
        return true;
    };
    bool n1 = tryRegisterService(QStringLiteral("org.freedesktop.PowerManagement"));
    bool n2 = tryRegisterService(QStringLiteral("org.freedesktop.ScreenSaver"));
    bool n3 = tryRegisterService(QStringLiteral("org.gnome.SessionManager"));

    m_watcher = new QDBusServiceWatcher(this);
    m_watcher->setConnection(bus);
    m_watcher->setWatchMode(QDBusServiceWatcher::WatchForUnregistration);
    connect(m_watcher, &QDBusServiceWatcher::serviceUnregistered,
            this, &InhibitBroker::onServiceUnregistered);

    m_dbusOk = any && (n1 || n2 || n3);
}

uint InhibitBroker::onInhibit(const QString &iface, const QString &senderUniqueName,
                              const QString &application, const QString &reason)
{
    if (application == QStringLiteral("Steam") || application == QStringLiteral("./steamwebhelper")) {
        qInfo().noquote() << QStringLiteral("Inhibit ignored from %1 app=%2").arg(iface).arg(application);
        return 0;
    }

    ++m_nextCookie;
    if (m_nextCookie == 0)
        ++m_nextCookie;
    Entry e;
    e.iface = iface;
    e.sender = senderUniqueName;
    e.application = application;
    e.reason = reason;
    m_entries.insert(m_nextCookie, e);
    if (m_watcher && !senderUniqueName.isEmpty() && senderUniqueName.startsWith(QLatin1Char(':')))
        m_watcher->addWatchedService(senderUniqueName);
    qInfo().noquote() << QStringLiteral("Inhibit cookie=%1 iface=%2 sender=%3 app=%4 reason=%5 (active=%6)")
                             .arg(m_nextCookie).arg(iface).arg(senderUniqueName)
                             .arg(application).arg(reason).arg(m_entries.size());
    recomputeActive(QStringLiteral("inhibit:%1").arg(application));
    return m_nextCookie;
}

void InhibitBroker::onUnInhibit(uint cookie)
{
    if (cookie == 0)
        return;
    auto it = m_entries.find(cookie);
    if (it == m_entries.end()) {
        qInfo().noquote() << QStringLiteral("UnInhibit unknown cookie=%1").arg(cookie);
        return;
    }
    const QString app = it->application;
    const QString sender = it->sender;
    m_entries.erase(it);

    bool stillUsed = false;
    for (const auto &e : std::as_const(m_entries)) {
        if (e.sender == sender) { stillUsed = true; break; }
    }
    if (m_watcher && !stillUsed && !sender.isEmpty() && sender.startsWith(QLatin1Char(':')))
        m_watcher->removeWatchedService(sender);

    qInfo().noquote() << QStringLiteral("UnInhibit cookie=%1 app=%2 (active=%3)")
                             .arg(cookie).arg(app).arg(m_entries.size());
    recomputeActive(QStringLiteral("uninhibit:%1").arg(app));
}

void InhibitBroker::onServiceUnregistered(const QString &service)
{
    QList<uint> drop;
    for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it) {
        if (it->sender == service)
            drop.append(it.key());
    }
    for (uint c : std::as_const(drop)) {
        qInfo().noquote() << QStringLiteral("Sender %1 vanished; dropping cookie=%2").arg(service).arg(c);
        m_entries.remove(c);
    }
    if (!drop.isEmpty()) {
        if (m_watcher) m_watcher->removeWatchedService(service);
        recomputeActive(QStringLiteral("disconnect:%1").arg(service));
    }
}

void InhibitBroker::recomputeActive(const QString &reason)
{
    const bool shouldBeActive = !m_entries.isEmpty();
    if (shouldBeActive && !m_active) {
        applyActive(reason);
    } else if (!shouldBeActive && m_active) {
        releaseActive(reason);
    } else if (shouldBeActive) {
        // Re-push to be idempotent on subsequent Inhibit calls.
        applyActive(reason);
    }
}

void InhibitBroker::applyActive(const QString &reason)
{
    const bool firstActivation = !m_active;
    m_active = true;
    if (!m_bridge)
        return;

    if (firstActivation) {
        m_restoreSnapshotValid = false;
        if (!m_configSnapshotValid) {
            m_configSnapshot = readSteamIdleSettings();
            m_configSnapshotValid = true;
            qInfo().noquote() << QStringLiteral("Steam idle helper captured session config snapshot: b_idle=%1 ac_idle=%2 b_susp=%3 ac_susp=%4")
                                     .arg(m_configSnapshot.batteryIdle)
                                     .arg(m_configSnapshot.acIdle)
                                     .arg(m_configSnapshot.batterySuspend)
                                     .arg(m_configSnapshot.acSuspend);
        }
        m_pendingActivateReason = reason;
        if (!m_snapshotCaptureInProgress) {
            m_snapshotCaptureInProgress = true;
            m_bridge->captureCurrentSettings(reason,
                                             [this](bool ok, const SteamIdleSettings &settings) {
                m_snapshotCaptureInProgress = false;
                if (ok) {
                    m_restoreSnapshot = settings;
                    m_restoreSnapshotValid = true;
                    qInfo().noquote() << QStringLiteral("Steam idle helper captured runtime restore snapshot: b_idle=%1 ac_idle=%2 b_susp=%3 ac_susp=%4")
                                             .arg(settings.batteryIdle)
                                             .arg(settings.acIdle)
                                             .arg(settings.batterySuspend)
                                             .arg(settings.acSuspend);
                } else {
                    m_restoreSnapshot = readSteamIdleSettings();
                    m_restoreSnapshotValid = true;
                    qWarning().noquote() << QStringLiteral("Steam idle helper failed to capture runtime restore snapshot; falling back to config/default values: b_idle=%1 ac_idle=%2 b_susp=%3 ac_susp=%4")
                                                .arg(m_restoreSnapshot.batteryIdle)
                                                .arg(m_restoreSnapshot.acIdle)
                                                .arg(m_restoreSnapshot.batterySuspend)
                                                .arg(m_restoreSnapshot.acSuspend);
                }

                if (!m_active || !m_bridge)
                    return;

                const QString activateReason = m_pendingActivateReason.isEmpty()
                                             ? QStringLiteral("activate")
                                             : m_pendingActivateReason;
                m_pendingActivateReason.clear();
                m_bridge->updateSettings(0, 0, 0, 0, activateReason);
            });
        }
        return;
    }

    if (m_snapshotCaptureInProgress) {
        m_pendingActivateReason = reason;
        return;
    }

    m_bridge->updateSettings(0, 0, 0, 0, reason);
}

void InhibitBroker::releaseActive(const QString &reason)
{
    m_active = false;
    m_pendingActivateReason.clear();
    if (m_snapshotCaptureInProgress) {
        qInfo().noquote() << QStringLiteral("Steam idle helper release requested while runtime snapshot was still pending; skipping restore because override was not applied yet");
        return;
    }
    if (m_bridge) {
        const SteamIdleSettings restore = restoreSettings();
        m_restoreSnapshotValid = false;
        qInfo().noquote() << QStringLiteral("Steam idle helper restoring previous values: b_idle=%1 ac_idle=%2 b_susp=%3 ac_susp=%4")
                                 .arg(restore.batteryIdle)
                                 .arg(restore.acIdle)
                                 .arg(restore.batterySuspend)
                                 .arg(restore.acSuspend);
        m_bridge->updateSettings(restore.batteryIdle, restore.acIdle,
                                 restore.batterySuspend, restore.acSuspend, reason);
    }
}

SteamIdleSettings InhibitBroker::restoreSettings()
{
    if (m_configSnapshotValid) {
        if (writeSteamIdleSettings(m_configSnapshot)) {
            qInfo().noquote() << QStringLiteral("Steam idle helper restored config.vdf idle values from session snapshot: b_idle=%1 ac_idle=%2 b_susp=%3 ac_susp=%4")
                                     .arg(m_configSnapshot.batteryIdle)
                                     .arg(m_configSnapshot.acIdle)
                                     .arg(m_configSnapshot.batterySuspend)
                                     .arg(m_configSnapshot.acSuspend);
        }
        return m_configSnapshot;
    }

    if (m_restoreSnapshotValid) {
        return m_restoreSnapshot;
    }
    return readSteamIdleSettings();
}

QString InhibitBroker::summaryReason() const
{
    QStringList parts;
    for (const auto &e : std::as_const(m_entries))
        parts << QStringLiteral("%1(%2)").arg(e.application).arg(e.iface);
    return parts.join(QLatin1Char(','));
}

void InhibitBroker::onLocalConnection()
{
    while (QLocalSocket *socket = m_server->nextPendingConnection()) {
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        if (!socket->waitForReadyRead(kLocalIoTimeoutMs)) {
            socket->write("error\n"); socket->flush(); socket->disconnectFromServer();
            continue;
        }
        const QList<QByteArray> parts = socket->readLine().trimmed().split('\t');
        const QString command = parts.value(0);
        const QString reason = parts.size() > 1 ? decodeReason(parts[1]) : QString();
        bool ok = true;
        if (command == QStringLiteral("activate")) {
            onInhibit(QStringLiteral("local"), QStringLiteral("aerocore"),
                      QStringLiteral("aerocore-browser"), reason);
        } else if (command == QStringLiteral("deactivate")) {
            QList<uint> drop;
            for (auto it = m_entries.cbegin(); it != m_entries.cend(); ++it)
                if (it->iface == QStringLiteral("local")) drop.append(it.key());
            for (uint c : std::as_const(drop)) m_entries.remove(c);
            recomputeActive(QStringLiteral("local-deactivate:%1").arg(reason));
        } else if (command == QStringLiteral("shutdown")) {
            socket->write("ok\n"); socket->flush();
            socket->waitForBytesWritten(kLocalIoTimeoutMs);
            socket->disconnectFromServer();
            shutdown(QStringLiteral("shutdown"));
            continue;
        } else {
            ok = false;
        }
        socket->write(ok ? "ok\n" : "error\n");
        socket->flush();
        socket->waitForBytesWritten(kLocalIoTimeoutMs);
        socket->disconnectFromServer();
    }
}

void InhibitBroker::onOwnerPoll()
{
    if (isProcessAlive(m_ownerPid))
        return;
    qWarning().noquote() << QStringLiteral("Steam idle helper detected owner exit; releasing");
    shutdown(QStringLiteral("owner-exit"));
}

void InhibitBroker::shutdown(const QString &reason)
{
    if (m_entries.isEmpty() && !m_active) {
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
        return;
    }

    auto quitSoon = []() {
        QTimer::singleShot(0, qApp, &QCoreApplication::quit);
    };

    m_entries.clear();
    if (m_active) {
        m_active = false;
        m_pendingActivateReason.clear();
        if (m_snapshotCaptureInProgress) {
            quitSoon();
            return;
        }
        if (m_bridge) {
            const SteamIdleSettings restore = restoreSettings();
            m_restoreSnapshotValid = false;
            qInfo().noquote() << QStringLiteral("Steam idle helper restoring previous values: b_idle=%1 ac_idle=%2 b_susp=%3 ac_susp=%4")
                                     .arg(restore.batteryIdle)
                                     .arg(restore.acIdle)
                                     .arg(restore.batterySuspend)
                                     .arg(restore.acSuspend);
            QMetaObject::Connection *conn = new QMetaObject::Connection();
            QTimer *timeout = new QTimer(this);
            timeout->setSingleShot(true);
            connect(timeout, &QTimer::timeout, this, [conn, timeout, quitSoon]() {
                QObject::disconnect(*conn);
                timeout->deleteLater();
                delete conn;
                quitSoon();
            });
            *conn = connect(m_bridge, &SteamFrontendBridge::idleStateChanged, this,
                            [conn, timeout, quitSoon, this]() {
                                if (!m_bridge || !m_bridge->isIdle())
                                    return;
                                QObject::disconnect(*conn);
                                timeout->stop();
                                timeout->deleteLater();
                                delete conn;
                                quitSoon();
                            });
            m_bridge->updateSettings(restore.batteryIdle, restore.acIdle,
                                     restore.batterySuspend, restore.acSuspend, reason);
            timeout->start(2000);
            if (m_bridge->isIdle()) {
                timeout->stop();
                timeout->deleteLater();
                QObject::disconnect(*conn);
                delete conn;
                quitSoon();
            }
            return;
        }
    }

    quitSoon();
}

// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("aerocore-steam-idle-helper"));
    installTerminationHandlers();

    QCommandLineParser parser;
    parser.addHelpOption();
    QCommandLineOption serverNameOption(QStringLiteral("server-name"),
        QStringLiteral("Local server name"), QStringLiteral("name"));
    QCommandLineOption ownerPidOption(QStringLiteral("owner-pid"),
        QStringLiteral("Owner process id"), QStringLiteral("pid"));
    parser.addOption(serverNameOption);
    parser.addOption(ownerPidOption);
    parser.process(app);

    const QString serverName = parser.value(serverNameOption).trimmed();
    const qint64 ownerPid = parser.value(ownerPidOption).toLongLong();
    if (serverName.isEmpty() || ownerPid <= 0) {
        qCritical().noquote() << QStringLiteral("Steam idle helper requires --server-name and --owner-pid");
        return 2;
    }

    auto *bridge = new SteamFrontendBridge(&app);
    InhibitBroker broker(serverName, ownerPid, bridge);
    if (!broker.start())
        return 1;

    QTimer terminationPoll;
    terminationPoll.setInterval(100);
    bool terminationShutdownRequested = false;
    QObject::connect(&terminationPoll, &QTimer::timeout, &app, [&broker, &terminationShutdownRequested]() {
        if (gHelperTerminationSignal == 0 || terminationShutdownRequested) {
            return;
        }
        const int signalNumber = gHelperTerminationSignal;
        gHelperTerminationSignal = 0;
        terminationShutdownRequested = true;
        qWarning().noquote() << QStringLiteral("Steam idle helper received termination signal: %1")
                                    .arg(signalNumber);
        broker.shutdown(QStringLiteral("signal:%1").arg(signalNumber));
    });
    terminationPoll.start();

    return app.exec();
}

#include "steamidleoverridehelper.moc"
