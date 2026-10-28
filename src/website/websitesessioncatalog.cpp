#include "website/websitesessioncatalog.h"

#include "website/website.h"
#include "website/websiterepository.h"

#include <QUrl>

#include <algorithm>

namespace {

QString normalizedUrlForCatalog(const QString &url)
{
    const QUrl normalized = QUrl::fromUserInput(url.trimmed());
    return normalized.isValid() ? normalized.toString(QUrl::RemoveFragment) : QString();
}

} // namespace

WebsiteSessionCatalog::WebsiteSessionCatalog(WebsiteRepository *repository)
    : m_repository(repository)
{
}

void WebsiteSessionCatalog::setRepository(WebsiteRepository *repository)
{
    m_repository = repository;
}

void WebsiteSessionCatalog::refresh()
{
    m_entries.clear();
    if (!m_repository) {
        return;
    }

    for (const Website &website : m_repository->listAll()) {
        const QString normalized = normalizedUrlForCatalog(website.url);
        if (normalized.isEmpty()) {
            continue;
        }

        SessionSiteEntry entry;
        entry.url = normalized;
        entry.title = website.name.trimmed().isEmpty() ? normalized : website.name.trimmed();
        m_entries.append(entry);
    }

    std::sort(m_entries.begin(), m_entries.end(), [](const SessionSiteEntry &lhs, const SessionSiteEntry &rhs) {
        return lhs.url.size() > rhs.url.size();
    });
}

int WebsiteSessionCatalog::count() const
{
    return m_entries.size();
}

QString WebsiteSessionCatalog::urlAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).url : QString();
}

QString WebsiteSessionCatalog::titleAt(int row) const
{
    return row >= 0 && row < m_entries.size() ? m_entries.at(row).title : QString();
}
