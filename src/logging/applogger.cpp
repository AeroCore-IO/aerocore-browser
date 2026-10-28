#include "logging/applogger.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QStandardPaths>

#include <cstdio>
#include <cstdlib>

namespace AppLogger {
namespace {

QMutex g_mutex;
QFile g_file;
QtMessageHandler g_previousHandler = nullptr;
bool g_initialized = false;

QString levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return QStringLiteral("D");
    case QtInfoMsg:
        return QStringLiteral("I");
    case QtWarningMsg:
        return QStringLiteral("W");
    case QtCriticalMsg:
        return QStringLiteral("C");
    case QtFatalMsg:
        return QStringLiteral("F");
    }
    return QStringLiteral("?");
}

QString resolveStateDir()
{
    const QByteArray overrideDir = qgetenv("AEROCORE_LOG_DIR").trimmed();
    if (!overrideDir.isEmpty()) {
        return QString::fromLocal8Bit(overrideDir);
    }

#if defined(_WIN32)
    const QString appData = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!appData.isEmpty()) {
        return QDir(appData).filePath(QStringLiteral("logs"));
    }
    return QDir::temp().filePath(QStringLiteral("aerocore-browser"));
#else
    const QByteArray xdgState = qgetenv("XDG_STATE_HOME").trimmed();
    if (!xdgState.isEmpty()) {
        return QDir(QString::fromLocal8Bit(xdgState)).filePath(QStringLiteral("aerocore-browser"));
    }
    return QDir::home().filePath(QStringLiteral(".local/state/aerocore-browser"));
#endif
}

bool shouldWrite(QtMsgType type)
{
    if (type != QtDebugMsg) {
        return true;
    }
    return debugEnabled();
}

void writeLine(QtMsgType type, const QString &message)
{
    const QString line = QStringLiteral("[%1] %2 %3\n")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs),
                                  levelName(type),
                                  message);

    const QByteArray utf8 = line.toUtf8();

    {
        QMutexLocker lock(&g_mutex);
        if (g_file.isOpen()) {
            g_file.write(utf8);
            if (type >= QtWarningMsg) {
                g_file.flush();
            }
        }
    }

    fwrite(utf8.constData(), 1, static_cast<size_t>(utf8.size()), stderr);
    fflush(stderr);
}

void messageHandler(QtMsgType type, const QMessageLogContext &context, const QString &message)
{
    Q_UNUSED(context);

    if (!shouldWrite(type)) {
        return;
    }

    writeLine(type, message);

    if (type == QtFatalMsg) {
        abort();
    }
}

} // namespace

QString logFilePath()
{
    const QByteArray overrideFile = qgetenv("AEROCORE_LOG_FILE").trimmed();
    if (!overrideFile.isEmpty()) {
        return QString::fromLocal8Bit(overrideFile);
    }
    return QDir(resolveStateDir()).filePath(QStringLiteral("app.log"));
}

bool debugEnabled()
{
    static const bool enabled = []() {
        if (qEnvironmentVariableIsSet("AEROCORE_DEBUG")) {
            const QByteArray raw = qgetenv("AEROCORE_DEBUG").trimmed().toLower();
            if (raw.isEmpty() || raw == "1" || raw == "true" || raw == "yes" || raw == "on") {
                return true;
            }
            if (raw == "0" || raw == "false" || raw == "no" || raw == "off") {
                return false;
            }
        }
        const QByteArray level = qgetenv("AEROCORE_LOG_LEVEL").trimmed().toLower();
        return level == "debug" || level == "trace";
    }();
    return enabled;
}

void init()
{
    QMutexLocker lock(&g_mutex);
    if (g_initialized) {
        return;
    }

    const QString path = logFilePath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    g_file.setFileName(path);
    if (!g_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        fprintf(stderr,
                "[AppLogger] failed to open %s: %s\n",
                qPrintable(path),
                qPrintable(g_file.errorString()));
    }

    g_previousHandler = qInstallMessageHandler(messageHandler);
    g_initialized = true;
    lock.unlock();

    writeLine(QtInfoMsg, QStringLiteral("AppLogger initialized path=%1 debug=%2")
                             .arg(path, debugEnabled() ? QStringLiteral("1") : QStringLiteral("0")));
}

void shutdown()
{
    QMutexLocker lock(&g_mutex);
    if (!g_initialized) {
        return;
    }

    if (g_file.isOpen()) {
        g_file.flush();
        g_file.close();
    }

    qInstallMessageHandler(g_previousHandler);
    g_previousHandler = nullptr;
    g_initialized = false;
}

} // namespace AppLogger
