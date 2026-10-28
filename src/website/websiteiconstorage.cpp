#include "website/websiteiconstorage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QUrl>

namespace WebsiteIconStorage {

namespace {

QString s_presetIconSourceDirectory;
QString s_iconsStorageDirectory;

QString defaultPresetIconSourceDirectory()
{
    return QDir(QCoreApplication::applicationDirPath()).filePath(
        QStringLiteral("assets/website-icons"));
}

QString defaultIconsStorageDirectory()
{
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataRoot.isEmpty()) {
        return {};
    }
    return QDir(appDataRoot).filePath(QStringLiteral("website-icons"));
}

bool isValidWebsiteId(const QString &websiteId)
{
    const QString trimmed = websiteId.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    return !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

QString downloadedIconFileName(const QString &websiteId)
{
    return websiteId + QStringLiteral(".png");
}

QImage scaleIconForStorage(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }

    constexpr int kMaxSize = 128;
    if (image.width() <= kMaxSize && image.height() <= kMaxSize) {
        return image;
    }
    return image.scaled(kMaxSize, kMaxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
}

} // namespace

QString presetIconSourceDirectory()
{
    if (!s_presetIconSourceDirectory.isEmpty()) {
        return s_presetIconSourceDirectory;
    }
    return defaultPresetIconSourceDirectory();
}

void setPresetIconSourceDirectory(const QString &directory)
{
    s_presetIconSourceDirectory = directory;
}

QString iconsStorageDirectory()
{
    if (!s_iconsStorageDirectory.isEmpty()) {
        return s_iconsStorageDirectory;
    }
    return defaultIconsStorageDirectory();
}

void setIconsStorageDirectory(const QString &directory)
{
    s_iconsStorageDirectory = directory;
}

QString installPresetIcon(const QString &iconFileName)
{
    if (iconFileName.isEmpty()) {
        return {};
    }

    const QString sourcePath = QDir(presetIconSourceDirectory()).filePath(iconFileName);
    if (!QFileInfo::exists(sourcePath)) {
        return {};
    }

    const QString destDir = iconsStorageDirectory();
    if (destDir.isEmpty()) {
        return {};
    }

    QDir().mkpath(destDir);
    const QString destPath = QDir(destDir).filePath(iconFileName);
    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }
    if (!QFile::copy(sourcePath, destPath)) {
        return {};
    }

    return QUrl::fromLocalFile(destPath).toString();
}

QString saveDownloadedIcon(const QString &websiteId, const QByteArray &data)
{
    if (!isValidWebsiteId(websiteId) || data.isEmpty()) {
        return {};
    }

    QImage image;
    if (!image.loadFromData(data)) {
        return {};
    }

    const QImage scaled = scaleIconForStorage(image);
    if (scaled.isNull()) {
        return {};
    }

    const QString destDir = iconsStorageDirectory();
    if (destDir.isEmpty()) {
        return {};
    }

    QDir().mkpath(destDir);
    const QString destPath = QDir(destDir).filePath(downloadedIconFileName(websiteId));
    if (QFile::exists(destPath)) {
        QFile::remove(destPath);
    }
    if (!scaled.save(destPath, "PNG")) {
        return {};
    }

    return QUrl::fromLocalFile(destPath).toString();
}

} // namespace WebsiteIconStorage
