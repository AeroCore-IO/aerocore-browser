#pragma once

#include "website/sessionsitecatalog.h"

#include <QList>
#include <QString>

class WebsiteRepository;

struct SessionSiteEntry
{
    QString url;
    QString title;
};

class WebsiteSessionCatalog : public SessionSiteCatalog
{
public:
    explicit WebsiteSessionCatalog(WebsiteRepository *repository = nullptr);

    void setRepository(WebsiteRepository *repository);
    void refresh();

    int count() const override;
    QString urlAt(int row) const override;
    QString titleAt(int row) const override;

private:
    WebsiteRepository *m_repository = nullptr;
    QList<SessionSiteEntry> m_entries;
};
