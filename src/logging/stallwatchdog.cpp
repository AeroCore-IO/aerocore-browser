#include "stallwatchdog.h"

#include <QDebug>
#include <QTimer>

namespace {
constexpr int kProbeIntervalMs = 10;
constexpr int kDefaultThresholdMs = 50;
}

StallWatchdog *StallWatchdog::installIfEnabled(QObject *parent)
{
    if (!qEnvironmentVariableIsSet("AEROCORE_STALL_MS")) {
        return nullptr;
    }

    int thresholdMs = kDefaultThresholdMs;
    const QByteArray raw = qgetenv("AEROCORE_STALL_MS").trimmed();
    if (!raw.isEmpty()) {
        bool ok = false;
        const int parsed = raw.toInt(&ok);
        if (ok && parsed > 0) {
            thresholdMs = parsed;
        }
    }

    auto *watchdog = new StallWatchdog(thresholdMs, parent);
    qInfo().noquote() << QStringLiteral("[stall] watchdog armed threshold=%1ms probe=%2ms")
                             .arg(thresholdMs)
                             .arg(kProbeIntervalMs);
    return watchdog;
}

StallWatchdog::StallWatchdog(int thresholdMs, QObject *parent)
    : QObject(parent)
    , m_thresholdMs(thresholdMs)
{
    m_timer = new QTimer(this);
    m_timer->setTimerType(Qt::PreciseTimer);
    connect(m_timer, &QTimer::timeout, this, &StallWatchdog::onTick);
    m_sinceLastTick.start();
    m_timer->start(kProbeIntervalMs);
}

void StallWatchdog::onTick()
{
    const qint64 gapMs = m_sinceLastTick.restart();
    if (gapMs < m_thresholdMs) {
        return;
    }

    ++m_stallCount;
    if (gapMs > m_worstGapMs) {
        m_worstGapMs = gapMs;
    }

    qWarning().noquote() << QStringLiteral("[stall] main thread blocked gap=%1ms count=%2 worst=%3ms")
                                .arg(gapMs)
                                .arg(m_stallCount)
                                .arg(m_worstGapMs);
}
