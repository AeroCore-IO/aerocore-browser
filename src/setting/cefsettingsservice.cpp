#include "setting/cefsettingsservice.h"

#include "setting/cefpaths.h"

#include <QCoreApplication>

CefSettingsService::CefSettingsService(QObject *parent)
    : QObject(parent)
{
}

QString CefSettingsService::cefProfilePath() const
{
    return CefPaths::profilePath();
}

QString CefSettingsService::cefLogPath() const
{
    return CefPaths::logPath();
}

QString CefSettingsService::appVersion() const
{
    return QCoreApplication::applicationVersion();
}
