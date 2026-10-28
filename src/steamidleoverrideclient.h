#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QString>

class QLocalSocket;
class QProcess;
class QTimer;

// Local-socket client for aerocore-steam-idle-helper.
//
// Runtime operations are non-blocking and driven by signals and single-shot timers. Never call
// waitForXxx or retry in a busy loop on the main thread. PowerInhibitor calls this client when
// video playback starts, while CEF OSR needs the main thread to keep pumping frames. The 10 ms
// timer drives CefDoMessageLoopWork(), and CEF callbacks including OnPaint are queued to the main
// thread. Blocking it freezes video while audio continues on Chromium's audio thread.
//
// The only exception is shutdownBlocking(), used during process exit when rendering has stopped.
// A bounded wait gives the helper time to restore the user's Steam idle settings through CDP.
class SteamIdleOverrideClient : public QObject
{
    Q_OBJECT

public:
    explicit SteamIdleOverrideClient(QObject *parent = nullptr);
    ~SteamIdleOverrideClient() override;

    // Sends the desired state without blocking. Calls are coalesced so only the latest state is
    // sent; a state matching the last successful request is skipped to avoid duplicate inhibit entries.
    void requestActive(bool active, const QString &reason);

    // Sends deactivate and shutdown during process exit, then waits for Steam idle settings to be restored.
    // Only the first call performs the shutdown.
    void shutdownBlocking();

    bool isAvailable() const;

signals:
    // Final result of an asynchronous request. requestedActive is the state sent in that attempt.
    void activeRequestFinished(bool requestedActive, bool succeeded);

private:
    enum class Phase {
        Idle,
        StartingHelper,
        Connecting,
        WaitingConnectRetry,
        AwaitingReply,
    };

    void scheduleAttempt();
    void startAttempt();
    void launchHelper();
    void beginConnect();
    void sendPendingCommand();
    void onAttemptTimeout();
    void abortAttempt(const QString &detail);
    void finishAttempt(bool succeeded, const QString &detail);
    void armAttemptTimer(int timeoutMs);
    bool sendCommandBlocking(const QString &command, const QString &reason);
    void detachHelperProcess();
    QString helperProgramPath() const;

    QProcess *m_process = nullptr;
    QLocalSocket *m_socket = nullptr;
    QTimer *m_attemptTimer = nullptr;
    QString m_serverName;

    bool m_available = false;
    bool m_shutdownDone = false;

    // Latest desired state and whether a newer request arrived during an attempt and must be sent afterward.
    bool m_desiredActive = false;
    QString m_desiredReason;
    bool m_requestPending = false;

    // Last successfully sent state; invalid means the helper's current state is unknown.
    bool m_appliedActive = false;
    bool m_appliedValid = false;
    // Whether any request succeeded during this process lifetime. Used at shutdown to determine
    // whether the helper may hold our inhibit entry and needs a blocking deactivate.
    bool m_helperEverAccepted = false;

    bool m_attemptInFlight = false;
    Phase m_phase = Phase::Idle;
    QString m_sentCommand;
    bool m_connectErrorLogged = false;
    QString m_lastFailureDetail;

    QElapsedTimer m_retryCooldown;
    QElapsedTimer m_attemptElapsed;
};
