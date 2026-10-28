#include "powerinhibitor.h"

#include "steamidleoverrideclient.h"

#include <QDebug>

#if defined(__linux__)
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCall>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusUnixFileDescriptor>
#include <QVariantMap>

#include <unistd.h>
#elif defined(_WIN32)
#include <windows.h>
#include <powrprof.h>
#endif

namespace {
constexpr int kActivationRetryCooldownMs = 5000;

#if defined(__linux__)
constexpr int kDbusCallTimeoutMs = 2000;

void closePortalRequest(const QString &handle)
{
    if (handle.isEmpty()) {
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
                                                       handle,
                                                       QStringLiteral("org.freedesktop.portal.Request"),
                                                       QStringLiteral("Close"));
    QDBusConnection::sessionBus().asyncCall(call, kDbusCallTimeoutMs);
}

void unInhibitScreenSaver(uint cookie)
{
    if (cookie == 0) {
        return;
    }
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"),
                                                       QStringLiteral("/ScreenSaver"),
                                                       QStringLiteral("org.freedesktop.ScreenSaver"),
                                                       QStringLiteral("UnInhibit"));
    call << cookie;
    QDBusConnection::sessionBus().asyncCall(call, kDbusCallTimeoutMs);
}
#endif

#if defined(_WIN32)
QString windowsPowerErrorMessage(DWORD error)
{
    LPWSTR buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr,
                                        error,
                                        0,
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    const QString message = (length > 0 && buffer)
        ? QString::fromWCharArray(buffer, int(length)).trimmed()
        : QStringLiteral("error %1").arg(error);
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}
#endif
}

PowerInhibitor::PowerInhibitor(QObject *parent)
    : QObject(parent)
{
#if defined(__linux__)
    m_steamIdleOverride = new SteamIdleOverrideClient(this);
#endif
}

PowerInhibitor::~PowerInhibitor()
{
    deactivate();
}

bool PowerInhibitor::isActive() const
{
    return m_active;
}

void PowerInhibitor::setActive(bool active, const QString &reason)
{
    if (active) {
        const QString nextReason = reason.trimmed().isEmpty()
            ? QStringLiteral("Foreground browser media playback")
            : reason.trimmed();
        if (m_active) {
            if (m_reason != nextReason) {
                qInfo().noquote() << QStringLiteral("Power inhibitor reason updated: %1").arg(nextReason);
                m_reason = nextReason;
            }
            return;
        }

        if (m_lastFailureAt.isValid()
            && m_lastFailedReason == nextReason
            && m_lastFailureAt.elapsed() < kActivationRetryCooldownMs) {
            return;
        }

#if defined(__linux__)
        if (m_inhibitRequested && m_pendingReplies > 0 && m_reason == nextReason) {
            return;
        }
#endif

        activate(nextReason);
        return;
    }

#if defined(__linux__)
    if (!m_active && !m_inhibitRequested) {
        m_reason.clear();
        return;
    }
#else
    if (!m_active) {
        return;
    }
#endif

    deactivate();
}

void PowerInhibitor::activate(const QString &reason)
{
#if defined(__linux__)
    const int generation = ++m_generation;
    m_inhibitRequested = true;
    m_reason = reason;
    m_lastFailedReason.clear();
    m_lastFailureAt.invalidate();
    m_pendingReplies = 3;

    {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                                           QStringLiteral("/org/freedesktop/login1"),
                                                           QStringLiteral("org.freedesktop.login1.Manager"),
                                                           QStringLiteral("Inhibit"));
        call << QStringLiteral("idle:sleep")
             << QStringLiteral("AeroCore Browser")
             << reason
             << QStringLiteral("block");
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::systemBus().asyncCall(call, kDbusCallTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
            watcher->deleteLater();
            QDBusPendingReply<QDBusUnixFileDescriptor> reply = *watcher;
            if (!reply.isValid()) {
                qWarning().noquote() << QStringLiteral("Failed to acquire logind idle inhibitor: %1")
                                            .arg(reply.error().message());
                noteReplySettled(generation);
                return;
            }
            const int fd = ::dup(reply.value().fileDescriptor());
            if (fd < 0) {
                qWarning().noquote() << QStringLiteral("Failed to duplicate logind inhibitor descriptor");
                noteReplySettled(generation);
                return;
            }
            if (generation != m_generation) {
                ::close(fd);
                return;
            }
            releaseLogind();
            m_logindFd = fd;
            refreshActiveState();
            noteReplySettled(generation);
        });
    }

    {
        QVariantMap options;
        options.insert(QStringLiteral("reason"), reason);
        options.insert(QStringLiteral("handle_token"), QStringLiteral("aerocore_inhibit"));
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.portal.Desktop"),
                                                           QStringLiteral("/org/freedesktop/portal/desktop"),
                                                           QStringLiteral("org.freedesktop.portal.Inhibit"),
                                                           QStringLiteral("Inhibit"));
        call << QStringLiteral("") << uint(12) << options;
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(call, kDbusCallTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
            watcher->deleteLater();
            QDBusPendingReply<QDBusObjectPath> reply = *watcher;
            if (!reply.isValid()) {
                qWarning().noquote() << QStringLiteral("Failed to acquire portal inhibit: %1")
                                            .arg(reply.error().message());
                noteReplySettled(generation);
                return;
            }
            const QString handle = reply.value().path();
            if (handle.isEmpty()) {
                qWarning().noquote() << QStringLiteral("portal inhibit returned an empty request handle");
                noteReplySettled(generation);
                return;
            }
            if (generation != m_generation) {
                closePortalRequest(handle);
                return;
            }
            releasePortal();
            m_portalRequestHandle = handle;
            refreshActiveState();
            noteReplySettled(generation);
        });
    }

    {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"),
                                                           QStringLiteral("/ScreenSaver"),
                                                           QStringLiteral("org.freedesktop.ScreenSaver"),
                                                           QStringLiteral("Inhibit"));
        call << QStringLiteral("AeroCore Browser") << reason;
        auto *watcher = new QDBusPendingCallWatcher(
            QDBusConnection::sessionBus().asyncCall(call, kDbusCallTimeoutMs), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation]() {
            watcher->deleteLater();
            QDBusPendingReply<uint> reply = *watcher;
            if (!reply.isValid()) {
                qWarning().noquote() << QStringLiteral("Failed to acquire screensaver inhibitor: %1")
                                            .arg(reply.error().message());
                noteReplySettled(generation);
                return;
            }
            const uint cookie = reply.value();
            if (cookie == 0) {
                qWarning().noquote() << QStringLiteral("screensaver inhibitor returned a zero cookie");
                noteReplySettled(generation);
                return;
            }
            if (generation != m_generation) {
                unInhibitScreenSaver(cookie);
                return;
            }
            releaseScreenSaver();
            m_screenSaverCookie = cookie;
            refreshActiveState();
            noteReplySettled(generation);
        });
    }

    if (m_steamIdleOverride) {
        m_steamIdleOverride->requestActive(true, reason);
    }

    return;
#elif defined(_WIN32)
    QStringList acquiredBackends;

    REASON_CONTEXT context{};
    context.Version = POWER_REQUEST_CONTEXT_VERSION;
    context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
    std::wstring wideReason = reason.toStdWString();
    context.Reason.SimpleReasonString = const_cast<wchar_t *>(wideReason.empty()
        ? L"AeroCore Browser media playback"
        : wideReason.c_str());

    HANDLE requestHandle = PowerCreateRequest(&context);
    if (!requestHandle || requestHandle == INVALID_HANDLE_VALUE) {
        qWarning().noquote() << QStringLiteral("Failed to create Windows power request: %1")
                                    .arg(windowsPowerErrorMessage(GetLastError()));
        return;
    }

    bool acquired = false;
    if (PowerSetRequest(requestHandle, PowerRequestSystemRequired)) {
        m_windowsSystemRequired = true;
        acquired = true;
        acquiredBackends.append(QStringLiteral("windows-system"));
    } else {
        qWarning().noquote() << QStringLiteral("Failed to acquire Windows system power request: %1")
                                    .arg(windowsPowerErrorMessage(GetLastError()));
    }

    if (PowerSetRequest(requestHandle, PowerRequestDisplayRequired)) {
        m_windowsDisplayRequired = true;
        acquired = true;
        acquiredBackends.append(QStringLiteral("windows-display"));
    } else {
        qWarning().noquote() << QStringLiteral("Failed to acquire Windows display power request: %1")
                                    .arg(windowsPowerErrorMessage(GetLastError()));
    }

    if (!acquired) {
        CloseHandle(requestHandle);
        m_lastFailedReason = reason;
        m_lastFailureAt.restart();
        qWarning().noquote() << QStringLiteral("Power inhibitor activation failed for all backends");
        return;
    }

    m_powerRequestHandle = requestHandle;
    m_active = true;
    m_reason = reason;
    m_lastFailedReason.clear();
    m_lastFailureAt.invalidate();
    qInfo().noquote() << QStringLiteral("Power inhibitor backends acquired: %1")
                             .arg(acquiredBackends.join(QStringLiteral(", ")));
    emit activeChanged(true);
    return;
#else
    Q_UNUSED(reason);
#endif
}

void PowerInhibitor::deactivate()
{
    QStringList releasedBackends;
#if defined(__linux__)
    ++m_generation;
    m_pendingReplies = 0;
    m_inhibitRequested = false;

    if (!m_portalRequestHandle.isEmpty()) {
        releasedBackends.append(QStringLiteral("portal"));
    }
    if (m_screenSaverCookie != 0) {
        releasedBackends.append(QStringLiteral("screensaver"));
    }
    if (m_logindFd >= 0) {
        releasedBackends.append(QStringLiteral("logind"));
    }

    releasePortal();
    releaseScreenSaver();
    releaseLogind();

    if (m_steamIdleOverride) {
        m_steamIdleOverride->requestActive(false, m_reason);
    }

#elif defined(_WIN32)
    if (m_powerRequestHandle) {
        HANDLE requestHandle = static_cast<HANDLE>(m_powerRequestHandle);

        if (m_windowsDisplayRequired) {
            if (PowerClearRequest(requestHandle, PowerRequestDisplayRequired)) {
                releasedBackends.append(QStringLiteral("windows-display"));
            } else {
                qWarning().noquote() << QStringLiteral("Failed to release Windows display power request: %1")
                                            .arg(windowsPowerErrorMessage(GetLastError()));
            }
            m_windowsDisplayRequired = false;
        }

        if (m_windowsSystemRequired) {
            if (PowerClearRequest(requestHandle, PowerRequestSystemRequired)) {
                releasedBackends.append(QStringLiteral("windows-system"));
            } else {
                qWarning().noquote() << QStringLiteral("Failed to release Windows system power request: %1")
                                            .arg(windowsPowerErrorMessage(GetLastError()));
            }
            m_windowsSystemRequired = false;
        }

        CloseHandle(requestHandle);
        m_powerRequestHandle = nullptr;
    }

#endif

    if (!releasedBackends.isEmpty()) {
        qInfo().noquote() << QStringLiteral("Power inhibitor backends released: %1")
                                 .arg(releasedBackends.join(QStringLiteral(", ")));
    }

    const bool wasActive = m_active;
    m_active = false;
    m_reason.clear();
    m_lastFailedReason.clear();
    m_lastFailureAt.invalidate();
    if (wasActive) {
        emit activeChanged(false);
    }
}

void PowerInhibitor::appendBackendStatus(QStringList &entries, const QString &backend, bool active) const
{
    entries.append(QStringLiteral("%1=%2").arg(backend, active ? QStringLiteral("on") : QStringLiteral("off")));
}

#if defined(__linux__)
void PowerInhibitor::refreshActiveState()
{
    const bool active = (m_logindFd >= 0)
                        || !m_portalRequestHandle.isEmpty()
                        || (m_screenSaverCookie != 0);
    if (m_active == active) {
        return;
    }
    m_active = active;
    emit activeChanged(active);
}

void PowerInhibitor::noteReplySettled(int generation)
{
    if (generation != m_generation) {
        return;
    }
    if (m_pendingReplies > 0) {
        --m_pendingReplies;
    }
    if (m_pendingReplies != 0) {
        return;
    }

    refreshActiveState();
    if (m_active) {
        QStringList acquired;
        if (m_logindFd >= 0) {
            acquired.append(QStringLiteral("logind"));
        }
        if (!m_portalRequestHandle.isEmpty()) {
            acquired.append(QStringLiteral("portal"));
        }
        if (m_screenSaverCookie != 0) {
            acquired.append(QStringLiteral("screensaver"));
        }
        qInfo().noquote() << QStringLiteral("Power inhibitor backends acquired: %1")
                                 .arg(acquired.join(QStringLiteral(", ")));
        return;
    }

    m_lastFailedReason = m_reason;
    m_lastFailureAt.restart();
    qWarning().noquote() << QStringLiteral("Power inhibitor: no DBus backend acquired (steam-idle-helper reports separately)");
}

void PowerInhibitor::releaseLogind()
{
    if (m_logindFd >= 0) {
        ::close(m_logindFd);
        m_logindFd = -1;
    }
}

void PowerInhibitor::releasePortal()
{
    if (!m_portalRequestHandle.isEmpty()) {
        closePortalRequest(m_portalRequestHandle);
        m_portalRequestHandle.clear();
    }
}

void PowerInhibitor::releaseScreenSaver()
{
    if (m_screenSaverCookie != 0) {
        unInhibitScreenSaver(m_screenSaverCookie);
        m_screenSaverCookie = 0;
    }
}
#endif
