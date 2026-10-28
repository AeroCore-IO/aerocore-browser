#include "history/historyitem.h"

#include <QUrl>

QString normalizeHistoryUrl(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    QString withScheme = trimmed;
    if (!trimmed.contains(QStringLiteral("://"))) {
        withScheme = QStringLiteral("https://") + trimmed;
    }

    const QUrl url(withScheme);
    if (!url.isValid() || url.host().isEmpty()) {
        return {};
    }

    const QString scheme = url.scheme().toLower();
    if (scheme != QStringLiteral("http") && scheme != QStringLiteral("https")) {
        return {};
    }

    QUrl normalized;
    normalized.setScheme(scheme);
    normalized.setHost(url.host());
    if (url.port() > 0) {
        normalized.setPort(url.port());
    }
    normalized.setPath(url.path());
    normalized.setQuery(url.query());

    const QString result = normalized.toString(QUrl::RemoveFragment);
    if (result.isEmpty() || result.startsWith(QStringLiteral("about:"))) {
        return {};
    }
    return result;
}

QJsonObject BrowsingHistoryItem::toJsonObject() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("website_id"), websiteId);
    obj.insert(QStringLiteral("website_name"), websiteName);
    obj.insert(QStringLiteral("title"), title);
    obj.insert(QStringLiteral("thumbnail"), thumbnail);
    obj.insert(QStringLiteral("url"), url);
    obj.insert(QStringLiteral("last_visit_time"), lastVisitTime.toUTC().toString(Qt::ISODate));
    obj.insert(QStringLiteral("visit_count"), visitCount);
    obj.insert(QStringLiteral("created_at"), createdAt.toUTC().toString(Qt::ISODate));
    obj.insert(QStringLiteral("updated_at"), updatedAt.toUTC().toString(Qt::ISODate));
    return obj;
}
