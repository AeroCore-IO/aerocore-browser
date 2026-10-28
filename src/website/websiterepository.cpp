#include "website/websiterepository.h"

#include "database/databasemanager.h"
#include "website/websiteiconstorage.h"
#include "website/websitepresets.h"

#include <QDateTime>
#include <QSqlError>
#include <QSqlQuery>
#include <QUuid>

namespace {

bool websiteUrlPrefixesHistoryUrl(const QString &websiteUrl, const QString &historyUrl)
{
    if (historyUrl == websiteUrl) {
        return true;
    }

    if (!historyUrl.startsWith(websiteUrl)) {
        return false;
    }

    if (websiteUrl.endsWith(QLatin1Char('/'))) {
        return true;
    }

    return historyUrl.at(websiteUrl.size()) == QLatin1Char('/');
}

} // namespace

QString WebsiteRepository::s_lastSqlError;

void WebsiteRepository::setLastSqlError(const QSqlQuery &query)
{
    s_lastSqlError = query.lastError().text();
}

void WebsiteRepository::setLastSqlError(const QString &message)
{
    s_lastSqlError = message;
}

QString WebsiteRepository::lastSqlError()
{
    return s_lastSqlError;
}

Website WebsiteRepository::websiteFromQuery(const QSqlQuery &query)
{
    Website website;
    website.id = query.value(QStringLiteral("id")).toString();
    website.icon = query.value(QStringLiteral("icon")).toString();
    website.name = query.value(QStringLiteral("name")).toString();
    website.url = query.value(QStringLiteral("url")).toString();
    website.createdAt = QDateTime::fromString(
        query.value(QStringLiteral("created_at")).toString(), Qt::ISODate);
    website.updatedAt = QDateTime::fromString(
        query.value(QStringLiteral("updated_at")).toString(), Qt::ISODate);
    return website;
}

bool WebsiteRepository::add(const QString &name, const QString &normalizedUrl, const QString &icon)
{
    if (!DatabaseManager::isInitialized()) {
        setLastSqlError(QStringLiteral("database not initialized"));
        return false;
    }

    const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlQuery query(DatabaseManager::database());
    if (!query.prepare(QStringLiteral(
            "INSERT INTO websites (id, icon, name, url, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?, ?)"))) {
        setLastSqlError(query);
        return false;
    }

    query.addBindValue(id);
    query.addBindValue(icon.isNull() ? QStringLiteral("") : icon);
    query.addBindValue(name);
    query.addBindValue(normalizedUrl);
    query.addBindValue(now);
    query.addBindValue(now);

    if (!query.exec()) {
        setLastSqlError(query);
        return false;
    }

    s_lastSqlError.clear();
    return true;
}

bool WebsiteRepository::remove(const QString &id)
{
    if (!DatabaseManager::isInitialized()) {
        return false;
    }

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral("DELETE FROM websites WHERE id = ?"));
    query.addBindValue(id);
    if (!query.exec()) {
        return false;
    }
    return query.numRowsAffected() > 0;
}

bool WebsiteRepository::update(const Website &website)
{
    if (!DatabaseManager::isInitialized()) {
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral(
        "UPDATE websites SET icon = COALESCE(?, ''), name = ?, url = ?, updated_at = ? WHERE id = ?"));
    query.addBindValue(website.icon);
    query.addBindValue(website.name);
    query.addBindValue(website.url);
    query.addBindValue(now);
    query.addBindValue(website.id);
    if (!query.exec()) {
        return false;
    }
    return query.numRowsAffected() > 0;
}

QList<Website> WebsiteRepository::listAll() const
{
    QList<Website> websites;
    if (!DatabaseManager::isInitialized()) {
        return websites;
    }

    QSqlQuery query(DatabaseManager::database());
    if (!query.exec(QStringLiteral("SELECT * FROM websites ORDER BY created_at ASC"))) {
        return websites;
    }

    while (query.next()) {
        websites.append(websiteFromQuery(query));
    }
    return websites;
}

bool WebsiteRepository::exists(const QString &id) const
{
    if (!DatabaseManager::isInitialized()) {
        return false;
    }

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral("SELECT 1 FROM websites WHERE id = ? LIMIT 1"));
    query.addBindValue(id);
    return query.exec() && query.next();
}

bool WebsiteRepository::urlExists(const QString &normalizedUrl, const QString &excludeId) const
{
    if (!DatabaseManager::isInitialized()) {
        return false;
    }

    QSqlQuery query(DatabaseManager::database());
    if (excludeId.isEmpty()) {
        query.prepare(QStringLiteral("SELECT 1 FROM websites WHERE url = ? LIMIT 1"));
        query.addBindValue(normalizedUrl);
    } else {
        query.prepare(QStringLiteral("SELECT 1 FROM websites WHERE url = ? AND id != ? LIMIT 1"));
        query.addBindValue(normalizedUrl);
        query.addBindValue(excludeId);
    }
    return query.exec() && query.next();
}

std::optional<Website> WebsiteRepository::findById(const QString &id) const
{
    if (!DatabaseManager::isInitialized()) {
        return std::nullopt;
    }

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral("SELECT * FROM websites WHERE id = ? LIMIT 1"));
    query.addBindValue(id);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return websiteFromQuery(query);
}

std::optional<Website> WebsiteRepository::findByUrl(const QString &normalizedUrl) const
{
    if (!DatabaseManager::isInitialized() || normalizedUrl.isEmpty()) {
        return std::nullopt;
    }

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral("SELECT * FROM websites WHERE url = ? LIMIT 1"));
    query.addBindValue(normalizedUrl);
    if (!query.exec() || !query.next()) {
        return std::nullopt;
    }
    return websiteFromQuery(query);
}

bool WebsiteRepository::updateIconIfEmpty(const QString &id, const QString &icon)
{
    if (!DatabaseManager::isInitialized() || id.isEmpty() || icon.isEmpty()) {
        return false;
    }

    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);

    QSqlQuery query(DatabaseManager::database());
    query.prepare(QStringLiteral(
        "UPDATE websites SET icon = ?, updated_at = ? "
        "WHERE id = ? AND (icon = '' OR icon IS NULL)"));
    query.addBindValue(icon);
    query.addBindValue(now);
    query.addBindValue(id);
    if (!query.exec()) {
        setLastSqlError(query);
        return false;
    }
    return query.numRowsAffected() > 0;
}

std::optional<Website> WebsiteRepository::findMatchingForUrl(const QString &normalizedUrl) const
{
    if (!DatabaseManager::isInitialized() || normalizedUrl.isEmpty()) {
        return std::nullopt;
    }

    std::optional<Website> bestMatch;
    int bestMatchLength = -1;
    for (const Website &website : listAll()) {
        if (!websiteUrlPrefixesHistoryUrl(website.url, normalizedUrl)) {
            continue;
        }

        const int matchLength = website.url.size();
        if (matchLength > bestMatchLength) {
            bestMatch = website;
            bestMatchLength = matchLength;
        }
    }

    return bestMatch;
}

bool WebsiteRepository::isEmpty() const
{
    if (!DatabaseManager::isInitialized()) {
        return true;
    }

    QSqlQuery query(DatabaseManager::database());
    if (!query.exec(QStringLiteral("SELECT COUNT(*) FROM websites"))) {
        return true;
    }
    if (!query.next()) {
        return true;
    }
    return query.value(0).toInt() == 0;
}

int WebsiteRepository::importPresetsIfEmpty()
{
    if (!DatabaseManager::isInitialized() || !isEmpty()) {
        return 0;
    }

    int imported = 0;
    const QList<PresetWebsite> presets = builtInPresetWebsites();
    for (const PresetWebsite &preset : presets) {
        const QString normalizedUrl = normalizeWebsiteUrl(preset.url);
        if (normalizedUrl.isEmpty() || urlExists(normalizedUrl)) {
            continue;
        }
        const QString icon = WebsiteIconStorage::installPresetIcon(preset.iconFileName);
        if (add(preset.name, normalizedUrl, icon)) {
            ++imported;
        }
    }
    return imported;
}
