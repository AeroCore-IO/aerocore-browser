#pragma once

#include <QHash>
#include <QObject>
#include <QString>

struct SteamIdleSettings {
    int batteryIdle = 0;
    int acIdle = 0;
    int batterySuspend = 0;
    int acSuspend = 0;
};

class QDBusServiceWatcher;
class QLocalServer;
class QLocalSocket;
class QTimer;

class SteamFrontendBridge;

class InhibitBroker : public QObject
{
    Q_OBJECT
public:
    InhibitBroker(const QString &localServerName,
                  qint64 ownerPid,
                  SteamFrontendBridge *bridge,
                  QObject *parent = nullptr);

    bool start();
    void shutdown(const QString &reason);

    // Called by DBus adaptors.
    uint onInhibit(const QString &iface, const QString &senderUniqueName,
                   const QString &application, const QString &reason);
    void onUnInhibit(uint cookie);

private slots:
    void onLocalConnection();
    void onOwnerPoll();
    void onServiceUnregistered(const QString &service);

private:
    struct Entry {
        QString iface;
        QString sender;     // unique bus name like ":1.42"
        QString application;
        QString reason;
    };

    void registerDBus();
    void applyActive(const QString &reason);
    void releaseActive(const QString &reason);
    void recomputeActive(const QString &reason);
    SteamIdleSettings restoreSettings();
    QString summaryReason() const;

    QString m_localServerName;
    qint64 m_ownerPid = 0;
    SteamFrontendBridge *m_bridge = nullptr;
    QLocalServer *m_server = nullptr;
    QDBusServiceWatcher *m_watcher = nullptr;
    QTimer *m_ownerTimer = nullptr;
    QHash<uint, Entry> m_entries;
    uint m_nextCookie = 0;
    bool m_active = false;
    bool m_dbusOk = false;
    SteamIdleSettings m_restoreSnapshot;
    bool m_restoreSnapshotValid = false;
    SteamIdleSettings m_configSnapshot;
    bool m_configSnapshotValid = false;
    bool m_snapshotCaptureInProgress = false;
    QString m_pendingActivateReason;
};
