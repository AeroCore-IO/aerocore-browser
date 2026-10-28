#pragma once

#include <QElapsedTimer>
#include <QObject>

class QTimer;

//
//
class StallWatchdog : public QObject
{
    Q_OBJECT

public:
    static StallWatchdog *installIfEnabled(QObject *parent);

private:
    explicit StallWatchdog(int thresholdMs, QObject *parent);

    void onTick();

    QTimer *m_timer = nullptr;
    QElapsedTimer m_sinceLastTick;
    int m_thresholdMs = 50;
    qint64 m_worstGapMs = 0;
    int m_stallCount = 0;
};
