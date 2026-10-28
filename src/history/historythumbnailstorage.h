#pragma once

#include <QImage>
#include <QString>

namespace HistoryThumbnailStorage {

QString thumbnailsStorageDirectory();
void setThumbnailsStorageDirectory(const QString &directory);

QString saveThumbnail(const QString &historyId, const QImage &image);
bool removeThumbnail(const QString &historyId);
bool clearAll();

} // namespace HistoryThumbnailStorage
