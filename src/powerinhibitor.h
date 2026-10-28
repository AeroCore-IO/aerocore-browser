#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>
#include <QStringList>

class SteamIdleOverrideClient;

class PowerInhibitor : public QObject
{
    Q_OBJECT

public:
    explicit PowerInhibitor(QObject *parent = nullptr);
    ~PowerInhibitor() override;

    bool isActive() const;

public slots:
    void setActive(bool active, const QString &reason);

signals:
    void activeChanged(bool active);

private:
    void activate(const QString &reason);
    void deactivate();
    void appendBackendStatus(QStringList &entries, const QString &backend, bool active) const;

#if defined(__linux__)
    void refreshActiveState();
    void noteReplySettled(int generation);
    void releaseLogind();
    void releasePortal();
    void releaseScreenSaver();
#endif

    bool m_active = false;
    QString m_reason;
    QString m_lastFailedReason;
    QElapsedTimer m_lastFailureAt;

#if defined(__linux__)
    int m_logindFd = -1;
    uint m_screenSaverCookie = 0;
    QString m_portalRequestHandle;
    SteamIdleOverrideClient *m_steamIdleOverride = nullptr;
    bool m_inhibitRequested = false;
    int m_generation = 0;
    int m_pendingReplies = 0;
#elif defined(_WIN32)
    void *m_powerRequestHandle = nullptr;
    bool m_windowsDisplayRequired = false;
    bool m_windowsSystemRequired = false;
#endif
};