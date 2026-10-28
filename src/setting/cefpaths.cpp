#include "setting/cefpaths.h"

#include <QDir>
#include <QStandardPaths>

namespace CefPaths {

QString profilePath()
{
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    return qEnvironmentVariable(
        "AEROCORE_CEF_PROFILE_DIR",
        QDir(appDataRoot).filePath(QStringLiteral("cef-profile")));
}

QString logPath()
{
    return QDir(profilePath()).filePath(QStringLiteral("cef.log"));
}

} // namespace CefPaths
