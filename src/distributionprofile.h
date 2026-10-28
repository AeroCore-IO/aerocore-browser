#pragma once

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

inline QJsonObject distributionProfile()
{
    QString executableDir = QCoreApplication::applicationDirPath();
#if defined(__linux__)
    // CEF renderer subprocesses read this before QApplication is constructed.
    if (executableDir.isEmpty()) {
        executableDir = QFileInfo(QFileInfo(QStringLiteral("/proc/self/exe")).symLinkTarget()).absolutePath();
    }
#endif
    QFile file(QDir(executableDir).filePath(
        QStringLiteral("config/distribution.json")));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}
