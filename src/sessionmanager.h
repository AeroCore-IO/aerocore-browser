#pragma once

#include <QDateTime>
#include <QObject>
#include <QList>
#include <QString>
#include <QStringList>

#include "website/sessionsitecatalog.h"

struct BrowserTab
{
    QString tabId;
    QString title;
    QString currentUrl;
    QString screenshot;
    QDateTime updatedAt;
};

struct BrowserSession
{
    QString sessionId;
    QString siteId;
    QString siteName;
    QString title;
    QString type = QStringLiteral("main");
    QString currentUrl;
    QString icon;
    QString parentSessionId;
    QString activeTabId;
    QList<BrowserTab> tabs;
    QDateTime updatedAt;
};

class SessionManager : public QObject
{
    Q_OBJECT

public:
    explicit SessionManager(SessionSiteCatalog *catalog, QObject *parent = nullptr);

    QString activeSessionId() const;
    QString activeSiteId() const;
    QString activeSessionUrl() const;
    QString activeSessionTitle() const;
    QString activeSiteName() const;
    QString activeTabId() const;
    QStringList activeTabSummaries() const;
    QStringList sessionSummaries() const;
    QString sessionActiveTabId(const QString &sessionId) const;
    QStringList tabIdsForSession(const QString &sessionId) const;

    QList<BrowserSession> allSessions() const;
    QList<BrowserSession> orderedSessions() const;
    QList<BrowserSession> sessionsForSite(const QString &siteId) const;
    QList<BrowserTab> tabsForSession(const QString &sessionId) const;
    int tabCountForSession(const QString &sessionId) const;
    QString siteIdForUrl(const QString &url) const;
    QString siteNameForUrl(const QString &url) const;
    QString sessionUrl(const QString &sessionId) const;

    QString ensureSiteSession(const QString &url, const QString &title = QString());
    QString activateOrCreateSiteSession(const QString &url);
    QString openUrlInNewSession(const QString &url, const QString &title = QString(), const QString &type = QStringLiteral("main"));
    QString activateSession(const QString &sessionId);
    QString activateTab(const QString &tabId);
    QString adjacentSessionId(int delta) const;
    QString activateAdjacentSession(int delta);
    QString openUrlInNewTab(const QString &url, const QString &title = QString());
    QString activateAdjacentTab(int delta);
    QString closeActiveTab(int fallbackDelta, QString *closedTabId = nullptr);
    /// Closes a tab in any session. fallbackDelta applies only when the closed tab was active and other tabs remain.
    /// Closing the last tab clears the tab list but retains the session for the caller to handle.
    QString closeTab(const QString &tabId, int fallbackDelta, QString *closedTabId = nullptr);
    QString sessionIdForTab(const QString &tabId) const;
    QString siteIdForSession(const QString &sessionId) const;
    int activeTabCount() const;
    int activeTabIndex() const;

    void updateActiveSession(const QString &url, const QString &title = QString());
    /// Restores sessions as lazy placeholders from a persisted snapshot: populates metadata without creating browser views or loading pages.
    /// Applies only when no sessions are open; restores the persisted active session, falling back to the first session if invalid.
    void restoreSessions(const QList<BrowserSession> &sessions, const QString &activeSessionId);
    void closeActiveSession();
    void closeSession(const QString &sessionId);
    void closeSessionForSite(const QString &siteId);
    void refreshIconForSiteId(const QString &siteId, const QString &icon);

signals:
    void sessionsChanged();
    void activeSessionChanged();

private:
    BrowserSession *findSessionMutable(const QString &sessionId);
    const BrowserSession *findSession(const QString &sessionId) const;
    BrowserTab *findTabMutable(BrowserSession *session, const QString &tabId);
    const BrowserTab *findTab(const BrowserSession *session, const QString &tabId) const;
    void collapseDuplicateTabs(BrowserSession *session);
    void ensureSessionHasActiveTab(BrowserSession *session);
    void syncSessionFromActiveTab(BrowserSession *session);
    void normalizeSession(BrowserSession *session);
    BrowserSession *findSessionBySiteMutable(const QString &siteId);
    const BrowserSession *findSessionBySite(const QString &siteId) const;
    QString displayNameForSiteId(const QString &siteId) const;
    QString defaultUrlForSiteId(const QString &siteId) const;
    int matchingBookmarkRowForUrl(const QString &normalizedUrl) const;
    int bookmarkRowForSessionKey(const QString &sessionKey) const;
    int indexOfSession(const QString &sessionId) const;
    QString createSession(const QString &siteId,
                          const QString &siteName,
                          const QString &url,
                          const QString &title,
                          const QString &type,
                          const QString &parentSessionId = QString());
    void setActiveSession(const QString &sessionId);
    QString normalizedUrl(const QString &url) const;
    QString siteIconForUrl(const QString &url) const;
    void backfillSessionIconIfNeeded(BrowserSession *session, const QString &url);

    SessionSiteCatalog *m_catalog = nullptr;
    QList<BrowserSession> m_sessions;
    QString m_activeSessionId;
};
