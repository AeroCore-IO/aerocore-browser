#pragma once

#include <QString>

namespace WebsiteIconStorage {

QString presetIconSourceDirectory();
void setPresetIconSourceDirectory(const QString &directory);

QString iconsStorageDirectory();
void setIconsStorageDirectory(const QString &directory);

QString installPresetIcon(const QString &iconFileName);

QString saveDownloadedIcon(const QString &websiteId, const QByteArray &data);

} // namespace WebsiteIconStorage
