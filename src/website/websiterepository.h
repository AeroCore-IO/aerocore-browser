#pragma once

#include "website/website.h"

#include <QList>
#include <optional>

class WebsiteRepository
{
public:
    bool add(const QString &name, const QString &normalizedUrl, const QString &icon = {});
    bool remove(const QString &id);
    bool update(const Website &website);
    QList<Website> listAll() const;
    bool exists(const QString &id) const;
    bool urlExists(const QString &normalizedUrl, const QString &excludeId = {}) const;
    std::optional<Website> findById(const QString &id) const;
    std::optional<Website> findByUrl(const QString &normalizedUrl) const;
    std::optional<Website> findMatchingForUrl(const QString &normalizedUrl) const;

    bool updateIconIfEmpty(const QString &id, const QString &icon);

    bool isEmpty() const;
    int importPresetsIfEmpty();

    static QString lastSqlError();

private:
    static Website websiteFromQuery(const class QSqlQuery &query);
    static void setLastSqlError(const QSqlQuery &query);
    static void setLastSqlError(const QString &message);

    static QString s_lastSqlError;
};
