#pragma once

#include "history/historyitem.h"

#include <QList>
#include <optional>

class WebsiteRepository;

class HistoryRepository
{
public:
    bool upsertVisit(const QString &title, const QString &url);
    QList<BrowsingHistoryItem> listAll() const;
    bool remove(const QString &id);
    bool clearAll();
    bool exists(const QString &id) const;
    std::optional<QString> idForUrl(const QString &url) const;
    std::optional<BrowsingHistoryItem> itemForUrl(const QString &url) const;
    bool setThumbnailIfEmpty(const QString &id, const QString &thumbnailUrl);
    bool touchUpdatedAt(const QString &id);
    QList<BrowsingHistoryItem> removeAssociatedWithWebsite(
        const QString &websiteId, const WebsiteRepository &websiteRepository);

    static QString lastSqlError();

private:
    static BrowsingHistoryItem historyFromQuery(const class QSqlQuery &query);
    static void setLastSqlError(const QSqlQuery &query);
    static void setLastSqlError(const QString &message);
    static std::optional<BrowsingHistoryItem> findByUrl(const QString &normalizedUrl);

    static QString s_lastSqlError;
};
