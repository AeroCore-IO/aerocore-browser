#include "updatecontroller.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimer>

UpdateController::UpdateController(QObject *parent)
    : QObject(parent)
    , m_process(new QProcess(this))
    , m_statePoller(new QTimer(this))
{
    m_statePoller->setInterval(250);
    connect(m_process, &QProcess::finished, this, &UpdateController::onProcessFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || !m_busy) return;
        m_statePoller->stop();
        setError(tr("Unable to start updater: %1").arg(m_process->errorString()));
        setPhase(QStringLiteral("failed"));
        m_busy = false;
        emit busyChanged();
    });
    connect(m_statePoller, &QTimer::timeout, this, &UpdateController::pollState);
    scheduleAutomaticCheck();
}

bool UpdateController::busy() const { return m_busy; }
QString UpdateController::phase() const { return m_phase; }
QString UpdateController::installedVersion() const { return m_installedVersion; }
QString UpdateController::latestVersion() const { return m_latestVersion; }
bool UpdateController::updateAvailable() const { return m_updateAvailable; }
QString UpdateController::error() const { return m_error; }

QString UpdateController::statusText() const
{
    if (!m_error.isEmpty()) {
        return m_error;
    }
    if (m_phase == QStringLiteral("checking")) return tr("Checking for updates…");
    if (m_phase == QStringLiteral("downloading")) return tr("Downloading update…");
    if (m_phase == QStringLiteral("verifying")) return tr("Verifying update…");
    if (m_phase == QStringLiteral("extracting")) return tr("Preparing update…");
    if (m_phase == QStringLiteral("installing")) return tr("Installing update…");
    if (m_phase == QStringLiteral("succeeded")) {
        return tr("Update complete. Quit and reopen the browser to apply the new version.");
    }
    if (m_phase == QStringLiteral("update_available")) {
        return tr("New version available: %1").arg(m_latestVersion);
    }
    if (m_phase == QStringLiteral("idle")) {
        return m_installedVersion.isEmpty()
            ? tr("Check for and install the latest version")
            : tr("Already up to date (%1)").arg(m_installedVersion);
    }
    return tr("Update status: %1").arg(m_phase);
}

QString UpdateController::updaterPath() const
{
    const QString configured = qEnvironmentVariable("AEROCORE_UPDATER_PATH");
    if (!configured.isEmpty()) return configured;

    const QDir appDir(QCoreApplication::applicationDirPath());
    const QStringList candidates = {
        appDir.filePath(QStringLiteral("updater/aerocore-browser-updater")),
        appDir.filePath(QStringLiteral("aerocore-browser-updater"))
    };
    for (const QString &candidate : candidates) {
        if (QFileInfo(candidate).isExecutable()) return candidate;
    }
    return candidates.first();
}

QString UpdateController::statePath() const
{
    return QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)
        + QStringLiteral("/updater/state.json");
}

bool releaseSourceUsesHttp(const QString &installDir)
{
    QFile source(QDir(installDir).filePath(QStringLiteral("config/release-source.json")));
    if (!source.open(QIODevice::ReadOnly)) return false;

    const QJsonObject config = QJsonDocument::fromJson(source.readAll()).object();
    return config.value(QStringLiteral("api_base")).toString().trimmed().startsWith(QStringLiteral("http://"));
}

void UpdateController::setPhase(const QString &phaseValue)
{
    if (m_phase == phaseValue) return;
    m_phase = phaseValue;
    emit stateChanged();
}

void UpdateController::setError(const QString &errorValue)
{
    if (m_error == errorValue) return;
    m_error = errorValue;
    emit stateChanged();
}

void UpdateController::start(const QString &command)
{
    if (m_busy) return;
    if (!QFileInfo(updaterPath()).isExecutable()) {
        setError(tr("Updater not found: %1").arg(updaterPath()));
        setPhase(QStringLiteral("failed"));
        return;
    }

    m_error.clear();
    m_updateAvailable = false;
    setPhase(command == QStringLiteral("check") ? QStringLiteral("checking")
                                                  : QStringLiteral("downloading"));
    m_busy = true;
    emit busyChanged();

    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("AEROCORE_BROWSER_INSTALL_DIR"),
                      QCoreApplication::applicationDirPath());
    environment.insert(QStringLiteral("AEROCORE_UPDATER_STATE"), statePath());
    if (releaseSourceUsesHttp(QCoreApplication::applicationDirPath())) {
        environment.insert(QStringLiteral("AEROCORE_ALLOW_INSECURE_HTTP"), QStringLiteral("1"));
    }
    m_process->setProcessEnvironment(environment);
    m_process->start(updaterPath(), {command});
    if (command == QStringLiteral("apply")) m_statePoller->start();
}

void UpdateController::check() { start(QStringLiteral("check")); }

void UpdateController::scheduleAutomaticCheck()
{
#if defined(AEROCORE_ENABLE_AUTO_UPDATE_CHECK)
    QTimer::singleShot(10 * 1000, this, [this]() {
        check();
    });
#endif
}
void UpdateController::apply()
{
    if (m_updateAvailable) start(QStringLiteral("apply"));
}

void UpdateController::reset()
{
    if (m_busy) return;
    m_error.clear();
    m_latestVersion.clear();
    m_updateAvailable = false;
    m_phase = QStringLiteral("idle");
    emit stateChanged();
}

void UpdateController::readStateFile()
{
    QFile file(statePath());
    if (!file.open(QIODevice::ReadOnly)) return;
    const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
    const QString statePhase = state.value(QStringLiteral("phase")).toString();
    if (!statePhase.isEmpty()) setPhase(statePhase);
    const QString latest = state.value(QStringLiteral("latest_version")).toString();
    if (!latest.isEmpty() && latest != m_latestVersion) {
        m_latestVersion = latest;
        emit stateChanged();
    }
    const QString errorValue = state.value(QStringLiteral("error")).toString();
    if (!errorValue.isEmpty()) setError(errorValue);
}

void UpdateController::pollState() { readStateFile(); }

void UpdateController::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    m_statePoller->stop();
    const QString output = QString::fromLocal8Bit(m_process->readAllStandardOutput());
    const QString standardError = QString::fromLocal8Bit(m_process->readAllStandardError()).trimmed();

    if (m_process->arguments().value(0) == QStringLiteral("check")) {
        const QStringList lines = output.split('\n', Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const int separator = line.indexOf('=');
            if (separator < 0) continue;
            const QString key = line.left(separator);
            const QString value = line.mid(separator + 1).trimmed();
            if (key == QStringLiteral("installed_version")) m_installedVersion = value;
            if (key == QStringLiteral("latest_version")) m_latestVersion = value;
            if (key == QStringLiteral("update_available")) m_updateAvailable = value == QStringLiteral("1");
        }
        setPhase(m_updateAvailable ? QStringLiteral("update_available") : QStringLiteral("idle"));
    } else {
        readStateFile();
        if (exitCode == 0 && exitStatus == QProcess::NormalExit) setPhase(QStringLiteral("succeeded"));
    }

    if (exitCode != 0 || exitStatus != QProcess::NormalExit) {
        setError(standardError.isEmpty() ? tr("Update failed") : standardError);
        setPhase(QStringLiteral("failed"));
    }
    m_busy = false;
    emit busyChanged();
    emit stateChanged();
}
