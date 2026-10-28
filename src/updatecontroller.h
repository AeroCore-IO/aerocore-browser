#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class QTimer;

class UpdateController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString phase READ phase NOTIFY stateChanged)
    Q_PROPERTY(QString installedVersion READ installedVersion NOTIFY stateChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY stateChanged)
    Q_PROPERTY(bool updateAvailable READ updateAvailable NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY stateChanged)

public:
    explicit UpdateController(QObject *parent = nullptr);

    bool busy() const;
    QString phase() const;
    QString installedVersion() const;
    QString latestVersion() const;
    bool updateAvailable() const;
    QString error() const;
    QString statusText() const;

    Q_INVOKABLE void check();
    Q_INVOKABLE void apply();
    Q_INVOKABLE void reset();

signals:
    void busyChanged();
    void stateChanged();

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void pollState();

private:
    void start(const QString &command);
    QString updaterPath() const;
    QString statePath() const;
    void readStateFile();
    void setPhase(const QString &phase);
    void setError(const QString &error);
    void scheduleAutomaticCheck();

    QProcess *m_process = nullptr;
    QTimer *m_statePoller = nullptr;
    bool m_busy = false;
    QString m_phase = QStringLiteral("idle");
    QString m_installedVersion;
    QString m_latestVersion;
    bool m_updateAvailable = false;
    QString m_error;
};
