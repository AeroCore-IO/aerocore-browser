#include "steamidleoverrideclient.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QLocalSocket>
#include <QProcess>
#include <QRandomGenerator>
#include <QTimer>

namespace {
// Budget from start() until the helper listens. The LISTENING marker is for logs only;
// the connection phase retries independently.
constexpr int kHelperStartTimeoutMs = 5000;
// Per-attempt connectToServer wait budget.
constexpr int kConnectStepTimeoutMs = 500;
// Retry interval while the helper is not listening yet. Uses the event loop, never busy-waits.
constexpr int kConnectRetryIntervalMs = 100;
// Overall connection budget before failure and backoff.
constexpr int kConnectTotalTimeoutMs = 5000;
// The helper snapshots runtime settings before activate, which may require a CDP round trip (~2 s).
constexpr int kReplyTimeoutMs = 4000;
// Backoff after a failed attempt to avoid repeatedly hitting the helper.
constexpr int kRetryBackoffMs = 3000;

// The following budgets are used only for blocking shutdown cleanup.
constexpr int kShutdownConnectTimeoutMs = 1000;
constexpr int kShutdownWriteTimeoutMs = 1000;
constexpr int kShutdownReplyTimeoutMs = 4000;
constexpr int kShutdownProcessTimeoutMs = 3000;

// If the helper is still running at exit, detach it so ~QProcess does not kill it before its
// owner-exit poll restores Steam's idle settings. Keep the detached QProcess reachable through a
// file-scope pointer so LeakSanitizer does not report it as a leak (detect_leaks is enabled).
QProcess *g_detachedHelperProcess = nullptr;

QByteArray encodeCommand(const QString &command, const QString &reason)
{
    QByteArray line = command.toUtf8();
    if (!reason.isEmpty()) {
        line.append('\t');
        line.append(reason.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    }
    line.append('\n');
    return line;
}
}

SteamIdleOverrideClient::SteamIdleOverrideClient(QObject *parent)
    : QObject(parent)
{
    m_process = new QProcess(this);
    m_socket = new QLocalSocket(this);
    m_attemptTimer = new QTimer(this);
    m_attemptTimer->setSingleShot(true);
    m_process->setProcessChannelMode(QProcess::MergedChannels);
    m_serverName = QStringLiteral("aerocore-steam-idle-%1-%2")
                       .arg(QCoreApplication::applicationPid())
                       .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    // After setting the server name explicitly, use the no-argument connectToServer(). In Qt 6,
    // the named overload can lose its fullServerName after abort() and report "Invalid name".
    // Keeping the name on the socket avoids that failure mode; logs include error and state details.
    m_socket->setServerName(m_serverName);

    connect(m_attemptTimer, &QTimer::timeout, this, &SteamIdleOverrideClient::onAttemptTimeout);

    connect(m_process, &QProcess::started, this, [this]() {
        if (m_phase != Phase::StartingHelper) {
            return;
        }
        m_attemptTimer->stop();
        // The helper may not be listening immediately after launch; the connection phase retries.
        beginConnect();
    });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        const QString detail = m_process->errorString().isEmpty()
            ? QString::number(int(error))
            : m_process->errorString();
        qWarning().noquote() << QStringLiteral("Steam idle helper process error: %1").arg(detail);
        m_available = false;
        m_appliedValid = false;
        if (m_phase == Phase::StartingHelper) {
            abortAttempt(QStringLiteral("helper failed to start: %1").arg(detail));
        }
    });
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this]() {
        const QString output = QString::fromUtf8(m_process->readAllStandardOutput()).trimmed();
        if (!output.isEmpty()) {
            qInfo().noquote() << QStringLiteral("[steam-idle-helper] %1").arg(output);
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int exitCode, QProcess::ExitStatus exitStatus) {
        qWarning().noquote() << QStringLiteral("Steam idle helper exited: code=%1 status=%2")
                                    .arg(exitCode)
                                    .arg(exitStatus == QProcess::NormalExit ? QStringLiteral("normal")
                                                                            : QStringLiteral("crash"));
        m_available = false;
        m_appliedValid = false;
        m_retryCooldown.restart();
        if (m_attemptInFlight) {
            abortAttempt(QStringLiteral("helper exited while a request was in flight"));
        }
    });

    connect(m_socket, &QLocalSocket::connected, this, [this]() {
        if (m_phase != Phase::Connecting) {
            return;
        }
        m_attemptTimer->stop();
        sendPendingCommand();
    });
    connect(m_socket, &QLocalSocket::readyRead, this, [this]() {
        if (m_phase != Phase::AwaitingReply) {
            m_socket->readAll();
            return;
        }
        if (!m_socket->canReadLine()) {
            return;
        }
        const QByteArray reply = m_socket->readLine().trimmed();
        if (reply == "ok") {
            finishAttempt(true, QStringLiteral("%1 accepted by helper").arg(m_sentCommand));
            return;
        }
        abortAttempt(QStringLiteral("helper rejected %1 with reply: %2")
                         .arg(m_sentCommand, QString::fromUtf8(reply)));
    });
    connect(m_socket, &QLocalSocket::disconnected, this, [this]() {
        // The helper closes each connection after one command (see onLocalConnection), so a
        // disconnect after the reply is expected. Only a disconnect while awaiting the reply fails.
        if (m_phase == Phase::AwaitingReply) {
            abortAttempt(QStringLiteral("helper closed the connection before replying to %1").arg(m_sentCommand));
        }
    });
    connect(m_socket, &QLocalSocket::errorOccurred, this, [this](QLocalSocket::LocalSocketError error) {
        if (m_phase != Phase::Connecting) {
            // The helper's normal disconnect after a completed command may report PeerClosed.
            return;
        }
        m_attemptTimer->stop();

        if (m_attemptElapsed.elapsed() < kConnectTotalTimeoutMs) {
            // The helper may not be listening yet, or its name/permissions may be wrong. Log each
            // error class once, then retry after kConnectRetryIntervalMs via the event loop.
            if (!m_connectErrorLogged) {
                m_connectErrorLogged = true;
                qInfo().noquote() << QStringLiteral("Steam idle helper connect pending on %1: %2 (error=%3 state=%4), retrying every %5ms")
                                         .arg(m_serverName,
                                              m_socket->errorString(),
                                              QString::number(int(error)),
                                              QString::number(int(m_socket->state())),
                                              QString::number(kConnectRetryIntervalMs));
            }
            m_phase = Phase::WaitingConnectRetry;
            QTimer::singleShot(kConnectRetryIntervalMs, this, [this]() {
                if (m_attemptInFlight && m_phase == Phase::WaitingConnectRetry) {
                    beginConnect();
                }
            });
            return;
        }

        abortAttempt(QStringLiteral("connect to %1 failed within %2ms: %3 (error=%4)")
                         .arg(m_serverName,
                              QString::number(kConnectTotalTimeoutMs),
                              m_socket->errorString(),
                              QString::number(int(error))));
    });
}

SteamIdleOverrideClient::~SteamIdleOverrideClient()
{
    // Shutdown cleanup must let the helper restore the user's Steam idle settings; blocking is allowed here.
    shutdownBlocking();
}

void SteamIdleOverrideClient::requestActive(bool active, const QString &reason)
{
    if (m_shutdownDone) {
        return;
    }

    m_desiredActive = active;
    m_desiredReason = reason;

    // Skip a state already sent successfully: each duplicate activate creates another inhibit entry.
    // Always resend when m_appliedValid is false.
    if (m_appliedValid && m_appliedActive == active) {
        return;
    }

    if (m_attemptInFlight) {
        // Save the latest desired state and send it after the current attempt to avoid overlapping commands.
        m_requestPending = true;
        return;
    }

    scheduleAttempt();
}

bool SteamIdleOverrideClient::isAvailable() const
{
    return m_available;
}

void SteamIdleOverrideClient::scheduleAttempt()
{
    if (m_attemptInFlight) {
        m_requestPending = true;
        return;
    }

    int delayMs = 0;
    if (m_retryCooldown.isValid() && m_retryCooldown.elapsed() < kRetryBackoffMs) {
        delayMs = kRetryBackoffMs - int(m_retryCooldown.elapsed());
    }
    // Wait through backoff with a timer so the main thread remains idle.
    QTimer::singleShot(delayMs, this, &SteamIdleOverrideClient::startAttempt);
}

void SteamIdleOverrideClient::startAttempt()
{
    if (m_shutdownDone || m_attemptInFlight) {
        return;
    }

    // Repeated scheduleAttempt() calls can queue multiple singleShot timers. Stop if an earlier
    // timer already succeeded, or a duplicate activate will create another inhibit cookie.
    if (!m_requestPending && m_appliedValid && m_appliedActive == m_desiredActive) {
        return;
    }

    // Event-loop jitter may fire the timer before backoff ends; reschedule instead of spinning.
    if (m_retryCooldown.isValid() && m_retryCooldown.elapsed() < kRetryBackoffMs) {
        scheduleAttempt();
        return;
    }

    m_attemptInFlight = true;
    m_connectErrorLogged = false;
    m_attemptElapsed.restart();

    if (m_process->state() == QProcess::NotRunning) {
        launchHelper();
        return;
    }

    beginConnect();
}

void SteamIdleOverrideClient::launchHelper()
{
    const QString programPath = helperProgramPath();
    if (!QFileInfo::exists(programPath)) {
        abortAttempt(QStringLiteral("helper executable missing: %1").arg(programPath));
        return;
    }

    const QStringList arguments{
        QStringLiteral("--server-name"),
        m_serverName,
        QStringLiteral("--owner-pid"),
        QString::number(QCoreApplication::applicationPid())
    };

    qInfo().noquote() << QStringLiteral("Starting Steam idle helper: %1 %2")
                             .arg(programPath, arguments.join(QLatin1Char(' ')));

    m_phase = Phase::StartingHelper;
    armAttemptTimer(kHelperStartTimeoutMs);
    // Do not call waitForStarted(); QProcess::started/errorOccurred arrive through the event loop.
    m_process->start(programPath, arguments);
}

void SteamIdleOverrideClient::beginConnect()
{
    m_phase = Phase::Connecting;
    if (m_socket->state() != QLocalSocket::UnconnectedState) {
        m_socket->abort();
    }
    m_socket->setServerName(m_serverName);
    m_socket->connectToServer();
    armAttemptTimer(kConnectStepTimeoutMs);
}

void SteamIdleOverrideClient::sendPendingCommand()
{
    const QString command = m_desiredActive ? QStringLiteral("activate")
                                            : QStringLiteral("deactivate");
    const QByteArray line = encodeCommand(command, m_desiredReason);

    m_sentCommand = command;
    m_phase = Phase::AwaitingReply;

    if (m_socket->write(line) != line.size()) {
        abortAttempt(QStringLiteral("short write while sending %1").arg(command));
        return;
    }
    // Do not wait for bytesWritten(); the event loop writes any remaining bytes after flush().
    m_socket->flush();
    armAttemptTimer(kReplyTimeoutMs);
}

void SteamIdleOverrideClient::onAttemptTimeout()
{
    switch (m_phase) {
    case Phase::StartingHelper:
        abortAttempt(QStringLiteral("helper did not start within %1ms").arg(kHelperStartTimeoutMs));
        return;
    case Phase::Connecting:
        if (m_attemptElapsed.elapsed() < kConnectTotalTimeoutMs) {
            m_phase = Phase::WaitingConnectRetry;
            QTimer::singleShot(kConnectRetryIntervalMs, this, [this]() {
                if (m_attemptInFlight && m_phase == Phase::WaitingConnectRetry) {
                    beginConnect();
                }
            });
            return;
        }
        abortAttempt(QStringLiteral("connect to %1 timed out after %2ms")
                         .arg(m_serverName, QString::number(kConnectTotalTimeoutMs)));
        return;
    case Phase::AwaitingReply:
        abortAttempt(QStringLiteral("helper did not reply to %1 within %2ms")
                         .arg(m_sentCommand, QString::number(kReplyTimeoutMs)));
        return;
    case Phase::WaitingConnectRetry:
    case Phase::Idle:
        return;
    }
}

void SteamIdleOverrideClient::armAttemptTimer(int timeoutMs)
{
    m_attemptTimer->start(timeoutMs);
}

void SteamIdleOverrideClient::abortAttempt(const QString &detail)
{
    if (!m_attemptInFlight) {
        return;
    }

    // Set the phase to Idle before abort() so resulting disconnected/errorOccurred signals are
    // ignored by phase guards instead of recursing here.
    if (m_socket->state() != QLocalSocket::UnconnectedState) {
        m_phase = Phase::Idle;
        m_socket->abort();
    }

    finishAttempt(false, detail);
}

void SteamIdleOverrideClient::finishAttempt(bool succeeded, const QString &detail)
{
    m_attemptTimer->stop();
    m_phase = Phase::Idle;
    m_attemptInFlight = false;

    m_available = succeeded;
    if (succeeded) {
        m_appliedActive = m_desiredActive;
        m_appliedValid = true;
        m_helperEverAccepted = true;
        m_retryCooldown.invalidate();
        m_lastFailureDetail.clear();
        qInfo().noquote() << QStringLiteral("Steam idle helper request finished: %1").arg(detail);
    } else {
        m_appliedValid = false;
        m_retryCooldown.restart();
        // Log each failure class once to avoid flooding the log with repeated errors.
        if (detail != m_lastFailureDetail) {
            m_lastFailureDetail = detail;
            qWarning().noquote() << QStringLiteral("Steam idle helper request failed: %1").arg(detail);
        }
    }

    emit activeRequestFinished(m_desiredActive, succeeded);

    if (m_requestPending) {
        m_requestPending = false;
        scheduleAttempt();
    }
}

void SteamIdleOverrideClient::shutdownBlocking()
{
    if (m_shutdownDone) {
        return;
    }
    m_shutdownDone = true;

    m_attemptTimer->stop();
    m_phase = Phase::Idle;
    m_attemptInFlight = false;
    m_requestPending = false;

    if (m_process->state() == QProcess::NotRunning) {
        // The helper never started or has already exited, so there is nothing to restore.
        return;
    }

    if (!m_helperEverAccepted) {
        // No command succeeded, so the helper holds no inhibit and has no settings snapshot.
        // A blocking deactivate/shutdown would only wait out its timeouts; detach and let its
        // owner-exit poll handle cleanup.
        detachHelperProcess();
        return;
    }

    // The helper closes each connection after one command, so each command needs its own connection.
    sendCommandBlocking(QStringLiteral("deactivate"), QString());
    sendCommandBlocking(QStringLiteral("shutdown"), QString());

    // Shutdown restores Steam idle settings through CDP (up to ~2 s), then exits. Wait for it.
    if (m_process->waitForFinished(kShutdownProcessTimeoutMs)) {
        return;
    }

    // Do not kill it if it has not exited; it may still be restoring Steam settings.
    qWarning().noquote() << QStringLiteral("Steam idle helper did not exit within %1ms; detaching so it can finish restoring Steam idle settings")
                                .arg(kShutdownProcessTimeoutMs);
    detachHelperProcess();
}

void SteamIdleOverrideClient::detachHelperProcess()
{
    // Detach so ~QProcess does not kill it. Its owner-exit poll will detect the parent exit and
    // finish cleanup; keep the pointer reachable at file scope to avoid an LSan false positive.
    m_process->setParent(nullptr);
    g_detachedHelperProcess = m_process;
}

bool SteamIdleOverrideClient::sendCommandBlocking(const QString &command, const QString &reason)
{
    if (m_socket->state() != QLocalSocket::ConnectedState) {
        m_socket->abort();
        m_socket->setServerName(m_serverName);
        m_socket->connectToServer();
        if (!m_socket->waitForConnected(kShutdownConnectTimeoutMs)) {
            qWarning().noquote() << QStringLiteral("Steam idle helper unreachable while sending %1: %2")
                                        .arg(command, m_socket->errorString());
            return false;
        }
    }

    const QByteArray line = encodeCommand(command, reason);
    if (m_socket->write(line) != line.size()
        || !m_socket->waitForBytesWritten(kShutdownWriteTimeoutMs)) {
        qWarning().noquote() << QStringLiteral("Steam idle helper failed to accept %1").arg(command);
        return false;
    }
    if (!m_socket->waitForReadyRead(kShutdownReplyTimeoutMs)) {
        qWarning().noquote() << QStringLiteral("Steam idle helper timed out waiting for reply to %1").arg(command);
        return false;
    }

    const QByteArray reply = m_socket->readLine().trimmed();
    // The helper disconnects after each command; close this attempt so the next can reconnect.
    m_socket->disconnectFromServer();
    return reply == "ok";
}

QString SteamIdleOverrideClient::helperProgramPath() const
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("aerocore-steam-idle-helper"));
}
