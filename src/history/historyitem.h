#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

struct BrowsingHistoryItem
{
    QString id;
    QString websiteId;
    QString websiteName;
    QString title;
    QString thumbnail;
    QString url;
    QDateTime lastVisitTime;
    int visitCount = 1;
    QDateTime createdAt;
    QDateTime updatedAt;

    QJsonObject toJsonObject() const;
};

QString normalizeHistoryUrl(const QString &input);
