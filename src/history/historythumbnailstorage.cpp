#include "history/historythumbnailstorage.h"

#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QUrl>

namespace HistoryThumbnailStorage {

namespace {

QString s_thumbnailsStorageDirectory;

QString defaultThumbnailsStorageDirectory()
{
    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (appDataRoot.isEmpty()) {
        return {};
    }
    return QDir(appDataRoot).filePath(QStringLiteral("history-thumbnails"));
}

QString thumbnailFileName(const QString &historyId)
{
    return historyId + QStringLiteral(".png");
}

bool isValidHistoryId(const QString &historyId)
{
    const QString trimmed = historyId.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    return !trimmed.contains(QLatin1Char('/')) && !trimmed.contains(QLatin1Char('\\'));
}

QImage scaleForStorage(const QImage &image)
{
    if (image.isNull()) {
        return {};
    }

    QImage scaled = image;
    const qreal dpr = std::max(image.devicePixelRatio(), 1.0);
    constexpr int kMaxLogicalWidth = 400;
    if (image.width() > qRound(kMaxLogicalWidth * dpr)) {
        scaled = image.scaledToWidth(qRound(kMaxLogicalWidth * dpr), Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(dpr);
    }
    return scaled;
}

} // namespace

QString thumbnailsStorageDirectory()
{
    if (!s_thumbnailsStorageDirectory.isEmpty()) {
        return s_thumbnailsStorageDirectory;
    }
    return defaultThumbnailsStorageDirectory();
}

void setThumbnailsStorageDirectory(const QString &directory)
{
    s_thumbnailsStorageDirectory = directory;
}

QString saveThumbnail(const QString &historyId, const QImage &image)
{
    if (!isValidHistoryId(historyId)) {
        return {};
    }

    const QImage scaled = scaleForStorage(image);
    if (scaled.isNull()) {
        return {};
    }

    const QString destDir = thumbnailsStorageDirectory();
    if (destDir.isEmpty()) {
        return {};
    }

    QDir().mkpath(destDir);
    const QString destPath = QDir(destDir).filePath(thumbnailFileName(historyId));
    if (!scaled.save(destPath, "PNG")) {
        return {};
    }

    return QUrl::fromLocalFile(destPath).toString();
}

bool removeThumbnail(const QString &historyId)
{
    if (!isValidHistoryId(historyId)) {
        return false;
    }

    const QString destDir = thumbnailsStorageDirectory();
    if (destDir.isEmpty()) {
        return false;
    }

    const QString destPath = QDir(destDir).filePath(thumbnailFileName(historyId));
    if (!QFile::exists(destPath)) {
        return true;
    }
    return QFile::remove(destPath);
}

bool clearAll()
{
    const QString destDir = thumbnailsStorageDirectory();
    if (destDir.isEmpty()) {
        return false;
    }

    QDir dir(destDir);
    if (!dir.exists()) {
        return true;
    }

    const QStringList entries = dir.entryList(QDir::Files);
    for (const QString &entry : entries) {
        if (!dir.remove(entry)) {
            return false;
        }
    }
    return true;
}

} // namespace HistoryThumbnailStorage
