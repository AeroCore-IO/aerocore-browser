#include "website/website.h"

#include <QUrl>

QString normalizeWebsiteUrl(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    if (trimmed.contains(QLatin1Char(' '))) {
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

    if (url.host() != QStringLiteral("localhost") && !url.host().contains(QLatin1Char('.'))) {
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

    return normalized.toString();
}

bool isValidWebsiteUrl(const QString &input, QString *normalizedUrl)
{
    const QString normalized = normalizeWebsiteUrl(input);
    if (normalized.isEmpty()) {
        return false;
    }
    if (normalizedUrl) {
        *normalizedUrl = normalized;
    }
    return true;
}

bool isValidWebsiteIcon(const QString &icon)
{
    if (icon.isEmpty()) {
        return true;
    }

    const QUrl url(icon);
    if (!url.isValid()) {
        return false;
    }

    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("file")
        || scheme == QStringLiteral("http")
        || scheme == QStringLiteral("https");
}

QJsonObject Website::toJsonObject() const
{
    QJsonObject obj;
    obj.insert(QStringLiteral("id"), id);
    obj.insert(QStringLiteral("icon"), icon);
    obj.insert(QStringLiteral("name"), name);
    obj.insert(QStringLiteral("url"), url);
    obj.insert(QStringLiteral("has_open"), hasOpen);
    obj.insert(QStringLiteral("is_current"), isCurrent);
    return obj;
}

Website Website::fromJsonObject(const QJsonObject &obj)
{
    Website website;
    website.id = obj.value(QStringLiteral("id")).toString();
    website.icon = obj.value(QStringLiteral("icon")).toString();
    website.name = obj.value(QStringLiteral("name")).toString();
    website.url = obj.value(QStringLiteral("url")).toString();
    if (obj.contains(QStringLiteral("has_open"))) {
        website.hasOpen = obj.value(QStringLiteral("has_open")).toBool();
    }
    if (obj.contains(QStringLiteral("is_current"))) {
        website.isCurrent = obj.value(QStringLiteral("is_current")).toBool();
    }
    return website;
}
