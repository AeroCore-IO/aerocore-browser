#include "history/historyrepository.h"

#include "database/databasemanager.h"
#include "website/websiterepository.h"

#include <QDateTime>
#include <QDebug>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>
#include <QUrl>
#include <optional>

QString HistoryRepository::s_lastSqlError;

void HistoryRepository::setLastSqlError(const QSqlQuery &query)
{
    s_lastSqlError = query.lastError().text();
}

void HistoryRepository::setLastSqlError(const QString &message)
{
    s_lastSqlError = message;
}

QString HistoryRepository::lastSqlError()
{
    return s_lastSqlError;
}

BrowsingHistoryItem HistoryRepository::historyFromQuery(const QSqlQuery &query)
{
    BrowsingHistoryItem item;
    item.id = query.value(QStringLiteral("id")).toString();
    item.websiteId = query.value(QStringLiteral("website_id")).toString();
    item.websiteName = query.value(QStringLiteral("website_name")).toString();
    item.title = query.value(QStringLiteral("title")).toString();
    item.thumbnail = query.value(QStringLiteral("thumbnail")).toString();
    item.url = query.value(QStringLiteral("url")).toString();
    item.lastVisitTime = QDateTime::fromString(
        query.value(QStringLiteral("last_visit_time")).toString(), Qt::ISODate);
    item.visitCount = query.value(QStringLiteral("visit_count")).toInt();
    item.createdAt = QDateTime::fromString(
        query.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    item.updatedAt = QDateTime::fromString(
        query.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
    return item;
}

std::optional<BrowsingHistoryItem> HistoryRepository::findByUrl(const QString &normalizedUrl)
{
    if (!DatabaseManager::isInitialized()) {
        return std::nullopt;
    }

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral("SELECT * FROM history WHERE url = ? LIMIT 1"));
    query.addBindValue(normalizedUrl);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return historyFromQuery(query);
}

bool HistoryRepository::upsertVisit(const QString &title, const QString &url)
{
    if (!DatabaseManager::isInitialized()) {
        setLastSqlError(QStringLiteral("database not initialized"));
        return false;
    }

    const QString normalizedUrl = normalizeHistoryUrl(url);
    if (normalizedUrl.isEmpty()) {
        setLastSqlError(QStringLiteral("invalid history url"));
        return false;
    }

    QString resolvedTitle = title.trimmed();
    if (resolvedTitle.isEmpty()) {
        resolvedTitle = QUrl(normalizedUrl).host();
    }

    const WebsiteRepository websiteRepository;
    const std::optional<Website> website = websiteRepository.findMatchingForUrl(normalizedUrl);
    if (!website) {
        qInfo().noquote() << QStringLiteral("HistoryRepository: skip visit, url does not belong to a website: %1")
                                 .arg(normalizedUrl);
        return false;
    }

    const QString websiteId = website->id;
    const QString websiteName = website->name;

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    if (const std::optional<BrowsingHistoryItem> existing = findByUrl(normalizedUrl)) {
        QSqlQuery query(DatabaseManager::database());
        if (!query.prepare(QStringLiteral(
                "UPDATE history SET website_id = ?, website_name = ?, title = ?, "
                "last_visit_time = ?, visit_count = visit_count + 1, updated_at = ? "
                "WHERE id = ?"))) {
            setLastSqlError(query);
            return false;
        }

        query.addBindValue(websiteId);
        query.addBindValue(websiteName);
        query.addBindValue(resolvedTitle);
        query.addBindValue(now);
        query.addBindValue(now);
        query.addBindValue(existing->id);
        if (!query.exec()) {
            setLastSqlError(query);
            return false;
        }

        s_lastSqlError.clear();
        return query.numRowsAffected() > 0;
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QSqlQuery query(DatabaseManager::database());
    if (!query.prepare(QStringLiteral(
            "INSERT INTO history ("
            "id, website_id, website_name, title, thumbnail, url, "
            "last_visit_time, visit_count, created_at, updated_at"
            ") VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"))) {
        setLastSqlError(query);
        return false;
    }

    query.addBindValue(id);
    query.addBindValue(websiteId);
    query.addBindValue(websiteName);
    query.addBindValue(resolvedTitle);
    query.addBindValue(QStringLiteral(""));
    query.addBindValue(normalizedUrl);
    query.addBindValue(now);
    query.addBindValue(1);
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        setLastSqlError(query);
        return false;
    }

    s_lastSqlError.clear();
    return true;
}

std::optional<QString> HistoryRepository::idForUrl(const QString &url) const
{
    if (const std::optional<BrowsingHistoryItem> item = itemForUrl(url)) {
        return item->id;
    }
    return std::nullopt;
}

std::optional<BrowsingHistoryItem> HistoryRepository::itemForUrl(const QString &url) const
{
    const QString normalizedUrl = normalizeHistoryUrl(url);
    if (normalizedUrl.isEmpty()) {
        return std::nullopt;
    }
    return findByUrl(normalizedUrl);
}

bool HistoryRepository::setThumbnailIfEmpty(const QString &id, const QString &thumbnailUrl)
{
    if (!DatabaseManager::isInitialized()) {
        setLastSqlError(QStringLiteral("database not initialized"));
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery query(DatabaseManager::database());
    if (!query.prepare(QStringLiteral(
            "UPDATE history SET thumbnail = ?, updated_at = ? "
            "WHERE id = ? AND thumbnail = ''"))) {
        setLastSqlError(query);
        return false;
    }

    query.addBindValue(thumbnailUrl);
    query.addBindValue(now);
    query.addBindValue(id);
    if (!query.exec()) {
        setLastSqlError(query);
        return false;
    }

    s_lastSqlError.clear();
    return query.numRowsAffected() > 0;
}

bool HistoryRepository::touchUpdatedAt(const QString &id)
{
    if (!DatabaseManager::isInitialized()) {
        setLastSqlError(QStringLiteral("database not initialized"));
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    QSqlQuery query(DatabaseManager::database());
    if (!query.prepare(QStringLiteral("UPDATE history SET updated_at = ? WHERE id = ?"))) {
        setLastSqlError(query);
        return false;
    }

    query.addBindValue(now);
    query.addBindValue(id);
    if (!query.exec()) {
        setLastSqlError(query);
        return false;
    }

    s_lastSqlError.clear();
    return query.numRowsAffected() > 0;
}

QList<BrowsingHistoryItem> HistoryRepository::listAll() const
{
    QList<BrowsingHistoryItem> items;
    if (!DatabaseManager::isInitialized()) {
        return items;
    }

    QSqlQuery query(DatabaseManager::database());
    if (!query.exec(QStringLiteral("SELECT * FROM history ORDER BY last_visit_time DESC"))) {
        return items;
    }

    while (query.next()) {
        items.append(historyFromQuery(query));
    }
    return items;
}

bool HistoryRepository::remove(const QString &id)
{
    if (!DatabaseManager::isInitialized()) {
        return false;
    }

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral("DELETE FROM history WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        return false;
    }
    return query.numRowsAffected() > 0;
}

QList<BrowsingHistoryItem> HistoryRepository::removeAssociatedWithWebsite(
    const QString &websiteId, const WebsiteRepository &websiteRepository)
{
    QList<BrowsingHistoryItem> removed;
    if (!DatabaseManager::isInitialized() || websiteId.isEmpty()) {
        return removed;
    }

    const QList<BrowsingHistoryItem> items = listAll();
    for (const BrowsingHistoryItem &item : items) {
        bool shouldRemove = item.websiteId == websiteId;
        if (!shouldRemove) {
            if (const std::optional<Website> match = websiteRepository.findMatchingForUrl(item.url)) {
                shouldRemove = match->id == websiteId;
            }
        }

        if (shouldRemove && remove(item.id)) {
            removed.append(item);
        }
    }

    return removed;
}

bool HistoryRepository::clearAll()
{
    if (!DatabaseManager::isInitialized()) {
        setLastSqlError(QStringLiteral("Database is not initialized"));
        return false;
    }

    QSqlQuery query(DatabaseManager::database());
    if (!query.exec(QStringLiteral("DELETE FROM history"))) {
        setLastSqlError(query);
        return false;
    }

    return true;
}

bool HistoryRepository::exists(const QString &id) const
{
    if (!DatabaseManager::isInitialized()) {
        return false;
    }

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral("SELECT 1 FROM history WHERE id = ? LIMIT 1"));
    query.addBindValue(id);
    return query.exec() && query.next();
}
