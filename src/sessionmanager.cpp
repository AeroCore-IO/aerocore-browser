#include "sessionmanager.h"

#include "website/sessionsitecatalog.h"
#include "database/databasemanager.h"
#include "website/websiterepository.h"

#include <QSet>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace {
bool isVirtualSingleTabSession(const BrowserSession &session)
{
    return session.tabs.isEmpty() && !session.currentUrl.isEmpty();
}

bool sessionOwnsTabId(const BrowserSession &session, const QString &tabId)
{
    if (tabId.isEmpty()) {
        return false;
    }
    if (isVirtualSingleTabSession(session) && session.activeTabId == tabId) {
        return true;
    }
    for (const BrowserTab &tab : session.tabs) {
        if (tab.tabId == tabId) {
            return true;
        }
    }
    return false;
}

int indexOfTabId(const BrowserSession &session, const QString &tabId)
{
    for (int index = 0; index < session.tabs.size(); ++index) {
        if (session.tabs.at(index).tabId == tabId) {
            return index;
        }
    }
    return -1;
}

QString normalizedUrlForStorage(const QString &url)
{
    const QUrl normalized = QUrl::fromUserInput(url.trimmed());
    return normalized.isValid() ? normalized.toString(QUrl::RemoveFragment) : QString();
}

int effectivePort(const QUrl &url)
{
    if (url.port() >= 0) {
        return url.port();
    }

    if (url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0) {
        return 443;
    }
    if (url.scheme().compare(QStringLiteral("http"), Qt::CaseInsensitive) == 0) {
        return 80;
    }

    return -1;
}

QString normalizedPath(const QUrl &url)
{
    const QString path = url.path();
    return path.isEmpty() ? QStringLiteral("/") : path;
}

bool sameOrigin(const QUrl &lhs, const QUrl &rhs)
{
    return lhs.scheme().compare(rhs.scheme(), Qt::CaseInsensitive) == 0
        && lhs.host().compare(rhs.host(), Qt::CaseInsensitive) == 0
        && effectivePort(lhs) == effectivePort(rhs);
}

bool bookmarkMatchesUrl(const QString &normalizedTarget, const QUrl &targetUrl, const QString &normalizedBookmark, const QUrl &bookmarkUrl)
{
    if (normalizedTarget == normalizedBookmark) {
        return true;
    }

    if (!bookmarkUrl.query().isEmpty() || !sameOrigin(targetUrl, bookmarkUrl)) {
        return false;
    }

    const QString bookmarkPath = normalizedPath(bookmarkUrl);
    if (bookmarkPath == QStringLiteral("/")) {
        return true;
    }

    const QString targetPath = normalizedPath(targetUrl);
    if (targetPath == bookmarkPath) {
        return true;
    }

    const QString prefix = bookmarkPath.endsWith(QLatin1Char('/')) ? bookmarkPath : bookmarkPath + QLatin1Char('/');
    return targetPath.startsWith(prefix);
}
}

SessionManager::SessionManager(SessionSiteCatalog *catalog, QObject *parent)
    : QObject(parent)
    , m_catalog(catalog)
{
}

QString SessionManager::activeSessionId() const
{
    return m_activeSessionId;
}

QString SessionManager::activeSiteId() const
{
    const BrowserSession *session = findSession(m_activeSessionId);
    return session ? session->siteId : QString();
}

QString SessionManager::activeSessionUrl() const
{
    const BrowserSession *session = findSession(m_activeSessionId);
    return session ? session->currentUrl : QString();
}

QString SessionManager::activeSessionTitle() const
{
    const BrowserSession *session = findSession(m_activeSessionId);
    return session ? session->title : QString();
}

QString SessionManager::activeSiteName() const
{
    const BrowserSession *session = findSession(m_activeSessionId);
    return session ? displayNameForSiteId(session->siteId) : QString();
}

QString SessionManager::activeTabId() const
{
    const BrowserSession *session = findSession(m_activeSessionId);
    return session ? session->activeTabId : QString();
}

QStringList SessionManager::activeTabSummaries() const
{
    const BrowserSession *session = findSession(m_activeSessionId);
    if (!session) {
        return {};
    }

    const QList<BrowserTab> tabs = session->tabs.isEmpty() && !session->currentUrl.isEmpty()
        ? QList<BrowserTab>{{session->activeTabId, session->title, session->currentUrl, QString(), session->updatedAt}}
        : session->tabs;
    QStringList summaries;
    for (int index = 0; index < tabs.size(); ++index) {
        const BrowserTab &tab = tabs.at(index);
        const bool active = tab.tabId == session->activeTabId || (session->activeTabId.isEmpty() && index == 0);
        const QString title = tab.title.trimmed().isEmpty() ? tab.currentUrl : tab.title.trimmed();
        summaries.append(QStringLiteral("%1 %2. %3")
                             .arg(active ? QStringLiteral(">") : QStringLiteral(" "))
                             .arg(index + 1)
                             .arg(title));
    }
    return summaries;
}

QStringList SessionManager::sessionSummaries() const
{
    const QList<BrowserSession> orderedSessions = this->orderedSessions();
    const QString activeSiteIdValue = activeSiteId();

    QStringList summaries;
    for (int index = 0; index < orderedSessions.size(); ++index) {
        const BrowserSession &session = orderedSessions.at(index);
        const QString name = displayNameForSiteId(session.siteId);
        const bool isActive = session.sessionId == m_activeSessionId
            || (!activeSiteIdValue.isEmpty() && session.siteId == activeSiteIdValue);
        summaries.append(QStringLiteral("%1 %2. %3")
                             .arg(isActive ? QStringLiteral(">") : QStringLiteral(" "))
                             .arg(index + 1)
                             .arg(name));
    }
    return summaries;
}

QString SessionManager::sessionActiveTabId(const QString &sessionId) const
{
    const BrowserSession *session = findSession(sessionId);
    return session ? session->activeTabId : QString();
}

QStringList SessionManager::tabIdsForSession(const QString &sessionId) const
{
    QStringList tabIds;
    const BrowserSession *session = findSession(sessionId);
    if (!session) {
        return tabIds;
    }

    for (const BrowserTab &tab : session->tabs) {
        if (!tab.tabId.isEmpty()) {
            tabIds.append(tab.tabId);
        }
    }

    if (tabIds.isEmpty() && !session->activeTabId.isEmpty()) {
        tabIds.append(session->activeTabId);
    }
    return tabIds;
}

int SessionManager::activeTabCount() const
{
    const BrowserSession *session = findSession(m_activeSessionId);
    if (!session) {
        return 0;
    }

    return session->tabs.isEmpty() && !session->currentUrl.isEmpty() ? 1 : session->tabs.size();
}

int SessionManager::activeTabIndex() const
{
    const BrowserSession *session = findSession(m_activeSessionId);
    if (!session) {
        return -1;
    }

    if (session->tabs.isEmpty()) {
        return session->currentUrl.isEmpty() ? -1 : 0;
    }

    for (int index = 0; index < session->tabs.size(); ++index) {
        if (session->tabs.at(index).tabId == session->activeTabId) {
            return index;
        }
    }

    return 0;
}

QList<BrowserSession> SessionManager::allSessions() const
{
    return m_sessions;
}

QList<BrowserSession> SessionManager::sessionsForSite(const QString &siteId) const
{
    QList<BrowserSession> matches;
    if (siteId.isEmpty()) {
        return matches;
    }

    for (const BrowserSession &session : m_sessions) {
        if (session.siteId == siteId) {
            matches.append(session);
        }
    }

    std::sort(matches.begin(), matches.end(), [](const BrowserSession &lhs, const BrowserSession &rhs) {
        return lhs.updatedAt > rhs.updatedAt;
    });
    return matches;
}

QString SessionManager::siteIdForUrl(const QString &url) const
{
    const QString normalized = normalizedUrl(url);
    if (normalized.isEmpty()) {
        return {};
    }

    const int bookmarkRow = matchingBookmarkRowForUrl(normalized);
    if (bookmarkRow >= 0) {
        return m_catalog->urlAt(bookmarkRow);
    }

    return QUrl(normalized).host().toLower();
}

QString SessionManager::siteNameForUrl(const QString &url) const
{
    const QString normalized = normalizedUrl(url);
    if (normalized.isEmpty()) {
        return {};
    }

    const int bookmarkRow = matchingBookmarkRowForUrl(normalized);
    if (bookmarkRow >= 0) {
        return m_catalog->titleAt(bookmarkRow);
    }

    return QUrl(normalized).host().toLower();
}

QString SessionManager::sessionUrl(const QString &sessionId) const
{
    const BrowserSession *session = findSession(sessionId);
    if (!session) {
        return {};
    }

    return session->currentUrl.trimmed().isEmpty() ? defaultUrlForSiteId(session->siteId) : session->currentUrl;
}

QString SessionManager::ensureSiteSession(const QString &url, const QString &title)
{
    const QString normalized = normalizedUrl(url);
    const QString siteId = siteIdForUrl(normalized);
    if (normalized.isEmpty() || siteId.isEmpty()) {
        return {};
    }

    if (BrowserSession *existingSession = findSessionBySiteMutable(siteId)) {
        normalizeSession(existingSession);
        if (existingSession->currentUrl.trimmed().isEmpty()) {
            existingSession->currentUrl = normalized;
        }
        if (!title.trimmed().isEmpty()) {
            existingSession->title = title.trimmed();
        }
        backfillSessionIconIfNeeded(existingSession, normalized);
        return existingSession->sessionId;
    }

    return createSession(siteId,
                         siteNameForUrl(normalized),
                         normalized,
                         title,
                         QStringLiteral("main"));
}

QString SessionManager::activateOrCreateSiteSession(const QString &url)
{
    const QString normalized = normalizedUrl(url);
    const QString siteId = siteIdForUrl(normalized);
    if (normalized.isEmpty() || siteId.isEmpty()) {
        return {};
    }

    const bool alreadyActive = findSessionBySite(siteId) != nullptr && m_activeSessionId == siteId;

    const QString sessionId = ensureSiteSession(url, siteNameForUrl(normalized));
    if (sessionId.isEmpty()) {
        return {};
    }

    if (alreadyActive && m_activeSessionId == sessionId) {
        const QString ensuredUrl = sessionUrl(sessionId);
        return ensuredUrl.isEmpty() ? normalized : ensuredUrl;
    }

    setActiveSession(sessionId);
    const QString ensuredUrl = sessionUrl(sessionId);
    return ensuredUrl.isEmpty() ? normalized : ensuredUrl;
}

QString SessionManager::openUrlInNewSession(const QString &url, const QString &title, const QString &type)
{
    Q_UNUSED(type);

    const QString normalized = normalizedUrl(url);
    const QString siteId = siteIdForUrl(normalized);
    if (normalized.isEmpty() || siteId.isEmpty()) {
        return {};
    }

    const QString sessionTitle = title.trimmed().isEmpty() ? siteNameForUrl(normalized) : title.trimmed();
    const QString sessionId = createSession(siteId,
                                            siteNameForUrl(normalized),
                                            normalized,
                                            sessionTitle,
                                            QStringLiteral("main"));
    setActiveSession(sessionId);
    emit sessionsChanged();
    return normalized;
}

QString SessionManager::activateSession(const QString &sessionId)
{
    const BrowserSession *session = findSession(sessionId);
    if (!session) {
        return {};
    }

    setActiveSession(sessionId);
    return session->currentUrl;
}

QString SessionManager::activateTab(const QString &tabId)
{
    if (tabId.isEmpty()) {
        return {};
    }

    for (BrowserSession &session : m_sessions) {
        BrowserTab *tab = findTabMutable(&session, tabId);
        if (!tab) {
            continue;
        }

        setActiveSession(session.sessionId);
        session.activeTabId = tabId;
        syncSessionFromActiveTab(&session);
        emit sessionsChanged();
        return session.currentUrl;
    }

    return {};
}

int SessionManager::tabCountForSession(const QString &sessionId) const
{
    const BrowserSession *session = findSession(sessionId);
    if (!session) {
        return 0;
    }

    return session->tabs.isEmpty() && !session->currentUrl.isEmpty() ? 1 : session->tabs.size();
}

QList<BrowserTab> SessionManager::tabsForSession(const QString &sessionId) const
{
    const BrowserSession *session = findSession(sessionId);
    if (!session) {
        return {};
    }

    if (session->tabs.isEmpty() && !session->currentUrl.isEmpty()) {
        BrowserTab tab;
        tab.tabId = session->activeTabId;
        tab.title = session->title;
        tab.currentUrl = session->currentUrl;
        tab.updatedAt = session->updatedAt;
        return {tab};
    }

    return session->tabs;
}

QString SessionManager::adjacentSessionId(int delta) const
{
    const QList<BrowserSession> sessions = orderedSessions();
    if (sessions.isEmpty()) {
        return {};
    }

    if (sessions.size() == 1) {
        return sessions.constFirst().sessionId;
    }

    int currentIndex = 0;
    for (int index = 0; index < sessions.size(); ++index) {
        if (sessions.at(index).sessionId == m_activeSessionId) {
            currentIndex = index;
            break;
        }

        if (!activeSiteId().isEmpty() && sessions.at(index).siteId == activeSiteId()) {
            currentIndex = index;
        }
    }

    const int nextIndex = (currentIndex + delta + sessions.size()) % sessions.size();
    return sessions.at(nextIndex).sessionId;
}

QString SessionManager::activateAdjacentSession(int delta)
{
    const QString sessionId = adjacentSessionId(delta);
    if (sessionId.isEmpty()) {
        return {};
    }

    return activateSession(sessionId);
}

QString SessionManager::openUrlInNewTab(const QString &url, const QString &title)
{
    const QString normalized = normalizedUrl(url);
    BrowserSession *session = findSessionMutable(m_activeSessionId);
    if (!session || normalized.isEmpty()) {
        return {};
    }

    ensureSessionHasActiveTab(session);

    for (const BrowserTab &existingTab : session->tabs) {
        if (existingTab.currentUrl == normalized) {
            session->activeTabId = existingTab.tabId;
            syncSessionFromActiveTab(session);
            emit sessionsChanged();
            return session->currentUrl;
        }
    }

    BrowserTab tab;
    tab.tabId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab.currentUrl = normalized;
    tab.title = title.trimmed().isEmpty() ? siteNameForUrl(normalized) : title.trimmed();
    tab.updatedAt = QDateTime::currentDateTimeUtc();

    session->tabs.append(tab);
    session->activeTabId = tab.tabId;
    syncSessionFromActiveTab(session);
    emit sessionsChanged();
    return session->currentUrl;
}

QString SessionManager::activateAdjacentTab(int delta)
{
    BrowserSession *session = findSessionMutable(m_activeSessionId);
    if (!session) {
        return {};
    }

    ensureSessionHasActiveTab(session);
    if (session->tabs.size() < 2) {
        return {};
    }

    int currentIndex = activeTabIndex();
    if (currentIndex < 0) {
        currentIndex = 0;
    }

    const QString currentUrl = session->tabs.at(currentIndex).currentUrl;
    int nextIndex = currentIndex;
    for (int step = 0; step < session->tabs.size(); ++step) {
        nextIndex = (nextIndex + delta + session->tabs.size()) % session->tabs.size();
        if (session->tabs.at(nextIndex).currentUrl != currentUrl || nextIndex == currentIndex) {
            break;
        }
    }
    session->activeTabId = session->tabs.at(nextIndex).tabId;
    syncSessionFromActiveTab(session);
    emit sessionsChanged();
    return session->currentUrl;
}

QString SessionManager::closeActiveTab(int fallbackDelta, QString *closedTabId)
{
    BrowserSession *session = findSessionMutable(m_activeSessionId);
    if (!session) {
        return {};
    }

    ensureSessionHasActiveTab(session);
    if (session->tabs.isEmpty()) {
        return {};
    }

    const int currentIndex = qMax(0, activeTabIndex());
    const QString removedTabId = session->tabs.at(currentIndex).tabId;
    return closeTab(removedTabId, fallbackDelta, closedTabId);
}

QString SessionManager::sessionIdForTab(const QString &tabId) const
{
    if (tabId.isEmpty()) {
        return {};
    }

    for (const BrowserSession &session : m_sessions) {
        if (sessionOwnsTabId(session, tabId)) {
            return session.sessionId;
        }
    }

    return {};
}

QString SessionManager::siteIdForSession(const QString &sessionId) const
{
    const BrowserSession *session = findSession(sessionId);
    return session ? session->siteId : QString();
}

QString SessionManager::closeTab(const QString &tabId, int fallbackDelta, QString *closedTabId)
{
    if (tabId.isEmpty()) {
        return {};
    }

    for (BrowserSession &session : m_sessions) {
        if (!sessionOwnsTabId(session, tabId)) {
            continue;
        }

        int tabIndex = -1;
        if (isVirtualSingleTabSession(session)) {
            ensureSessionHasActiveTab(&session);
            tabIndex = session.tabs.isEmpty() ? -1 : 0;
        } else {
            tabIndex = indexOfTabId(session, tabId);
        }
        if (tabIndex < 0) {
            return {};
        }

        const QString removedTabId = session.tabs.at(tabIndex).tabId;
        if (closedTabId) {
            *closedTabId = removedTabId;
        }

        if (session.tabs.size() == 1) {
            session.tabs.clear();
            session.activeTabId.clear();
            session.currentUrl.clear();
            session.title = session.siteName;
            session.updatedAt = QDateTime::currentDateTimeUtc();
            emit sessionsChanged();
            return {};
        }

        const bool closingActiveTab = (session.activeTabId == removedTabId);
        session.tabs.removeAt(tabIndex);

        if (closingActiveTab) {
            int nextIndex = tabIndex + fallbackDelta;
            if (nextIndex < 0) {
                nextIndex = session.tabs.size() - 1;
            } else if (nextIndex >= session.tabs.size()) {
                nextIndex = 0;
            }
            session.activeTabId = session.tabs.at(nextIndex).tabId;
            syncSessionFromActiveTab(&session);
        }

        emit sessionsChanged();
        return session.currentUrl;
    }

    return {};
}

void SessionManager::updateActiveSession(const QString &url, const QString &title)
{
    const QString normalized = normalizedUrl(url);
    if (normalized.isEmpty()) {
        return;
    }

    BrowserSession *session = findSessionMutable(m_activeSessionId);
    const QString siteId = session ? session->siteId : siteIdForUrl(normalized);
    if (siteId.isEmpty()) {
        return;
    }

    if (!session) {
        const QString sessionId = createSession(siteId,
                                                siteNameForUrl(normalized),
                                                normalized,
                                                title,
                                                QStringLiteral("main"));
        setActiveSession(sessionId);
        session = findSessionMutable(sessionId);
    }

    ensureSessionHasActiveTab(session);
    BrowserTab *tab = findTabMutable(session, session->activeTabId);

    bool changed = false;
    if (session->currentUrl != normalized) {
        session->currentUrl = normalized;
        changed = true;
    }

    if (tab && tab->currentUrl != normalized) {
        tab->currentUrl = normalized;
        changed = true;
    }

    const QString sessionTitle = title.trimmed().isEmpty() ? session->title : title.trimmed();
    if (session->title != sessionTitle) {
        session->title = sessionTitle;
        changed = true;
    }

    if (tab && tab->title != sessionTitle) {
        tab->title = sessionTitle;
        changed = true;
    }

    const QDateTime now = QDateTime::currentDateTimeUtc();
    if (session->updatedAt != now) {
        session->updatedAt = now;
        changed = true;
    }

    if (tab && tab->updatedAt != now) {
        tab->updatedAt = now;
        changed = true;
    }

    const int tabCountBeforeCollapse = session->tabs.size();
    const QString activeTabIdBeforeCollapse = session->activeTabId;
    collapseDuplicateTabs(session);
    if (session->tabs.size() != tabCountBeforeCollapse || session->activeTabId != activeTabIdBeforeCollapse) {
        changed = true;
    }

    if (!changed) {
        return;
    }

    normalizeSession(session);
    emit sessionsChanged();
}

void SessionManager::restoreSessions(const QList<BrowserSession> &sessions, const QString &activeSessionId)
{
    if (!m_sessions.isEmpty()) {
        return;
    }

    for (BrowserSession session : sessions) {
        if (session.currentUrl.trimmed().isEmpty()) {
            continue;
        }

        normalizeSession(&session);
        if (session.siteId.isEmpty() || findSessionBySite(session.siteId)) {
            continue;
        }

        QList<BrowserTab> validTabs;
        for (const BrowserTab &tab : session.tabs) {
            if (!tab.currentUrl.trimmed().isEmpty()) {
                validTabs.append(tab);
            }
        }
        session.tabs = validTabs;
        ensureSessionHasActiveTab(&session);

        backfillSessionIconIfNeeded(&session, session.currentUrl);
        m_sessions.append(session);
    }

    if (m_sessions.isEmpty()) {
        return;
    }

    m_activeSessionId = findSession(activeSessionId) ? activeSessionId
                                                     : m_sessions.constFirst().sessionId;
    emit sessionsChanged();
}

void SessionManager::closeActiveSession()
{
    const BrowserSession *session = findSession(m_activeSessionId);
    if (!session) {
        return;
    }

    closeSessionForSite(session->siteId);
}

void SessionManager::closeSession(const QString &sessionId)
{
    const int sessionIndex = indexOfSession(sessionId);
    if (sessionIndex < 0) {
        return;
    }

    const bool removedActive = (m_sessions.at(sessionIndex).sessionId == m_activeSessionId);
    m_sessions.removeAt(sessionIndex);

    if (removedActive) {
        m_activeSessionId.clear();
        emit activeSessionChanged();
    }

    emit sessionsChanged();
}

void SessionManager::closeSessionForSite(const QString &siteId)
{
    if (siteId.isEmpty()) {
        return;
    }

    bool removedAny = false;
    bool removedActive = false;
    for (int index = m_sessions.size() - 1; index >= 0; --index) {
        if (m_sessions.at(index).siteId != siteId) {
            continue;
        }

        removedActive = removedActive || (m_sessions.at(index).sessionId == m_activeSessionId);
        m_sessions.removeAt(index);
        removedAny = true;
    }

    if (!removedAny) {
        return;
    }

    if (removedActive) {
        m_activeSessionId.clear();
        emit activeSessionChanged();
    }

    emit sessionsChanged();
}

BrowserSession *SessionManager::findSessionMutable(const QString &sessionId)
{
    const int sessionIndex = indexOfSession(sessionId);
    return sessionIndex >= 0 ? &m_sessions[sessionIndex] : nullptr;
}

const BrowserSession *SessionManager::findSession(const QString &sessionId) const
{
    const int sessionIndex = indexOfSession(sessionId);
    return sessionIndex >= 0 ? &m_sessions.at(sessionIndex) : nullptr;
}

BrowserTab *SessionManager::findTabMutable(BrowserSession *session, const QString &tabId)
{
    if (!session || tabId.isEmpty()) {
        return nullptr;
    }

    for (BrowserTab &tab : session->tabs) {
        if (tab.tabId == tabId) {
            return &tab;
        }
    }

    return nullptr;
}

const BrowserTab *SessionManager::findTab(const BrowserSession *session, const QString &tabId) const
{
    if (!session || tabId.isEmpty()) {
        return nullptr;
    }

    for (const BrowserTab &tab : session->tabs) {
        if (tab.tabId == tabId) {
            return &tab;
        }
    }

    return nullptr;
}

void SessionManager::collapseDuplicateTabs(BrowserSession *session)
{
    if (!session || session->tabs.size() < 2) {
        return;
    }

    QList<BrowserTab> dedupedTabs;
    QHash<QString, int> tabIndexByUrl;
    const QString activeTabId = session->activeTabId;

    for (const BrowserTab &tab : session->tabs) {
        const QString urlKey = tab.currentUrl;
        if (urlKey.isEmpty() || !tabIndexByUrl.contains(urlKey)) {
            tabIndexByUrl.insert(urlKey, dedupedTabs.size());
            dedupedTabs.append(tab);
            continue;
        }

        BrowserTab &existingTab = dedupedTabs[tabIndexByUrl.value(urlKey)];
        const bool existingIsActive = existingTab.tabId == activeTabId;
        const bool incomingIsActive = tab.tabId == activeTabId;
        if (incomingIsActive || (!existingIsActive && tab.updatedAt > existingTab.updatedAt)) {
            existingTab = tab;
        }
    }

    session->tabs = dedupedTabs;
    if (!findTab(session, session->activeTabId) && !session->tabs.isEmpty()) {
        session->activeTabId = session->tabs.constFirst().tabId;
    }
    syncSessionFromActiveTab(session);
}

void SessionManager::ensureSessionHasActiveTab(BrowserSession *session)
{
    if (!session) {
        return;
    }

    if (session->tabs.isEmpty() && !session->currentUrl.isEmpty()) {
        BrowserTab tab;
        tab.tabId = QUuid::createUuid().toString(QUuid::WithoutBraces);
        tab.title = session->title.trimmed().isEmpty() ? session->siteName : session->title;
        tab.currentUrl = session->currentUrl;
        tab.updatedAt = session->updatedAt.isValid() ? session->updatedAt : QDateTime::currentDateTimeUtc();
        session->activeTabId = tab.tabId;
        session->tabs.append(tab);
        return;
    }

    if (!session->tabs.isEmpty() && !findTab(session, session->activeTabId)) {
        session->activeTabId = session->tabs.constFirst().tabId;
    }
}

void SessionManager::syncSessionFromActiveTab(BrowserSession *session)
{
    if (!session) {
        return;
    }

    ensureSessionHasActiveTab(session);
    const BrowserTab *tab = findTab(session, session->activeTabId);
    if (!tab) {
        return;
    }

    session->currentUrl = tab->currentUrl;
    session->title = tab->title.trimmed().isEmpty() ? session->siteName : tab->title;
    session->updatedAt = QDateTime::currentDateTimeUtc();
    if (BrowserTab *mutableTab = findTabMutable(session, session->activeTabId)) {
        mutableTab->updatedAt = session->updatedAt;
    }
}

void SessionManager::normalizeSession(BrowserSession *session)
{
    if (!session) {
        return;
    }

    if (session->siteId.isEmpty() && !session->currentUrl.isEmpty()) {
        session->siteId = siteIdForUrl(session->currentUrl);
    }
    if (session->siteId.isEmpty()) {
        return;
    }

    session->sessionId = session->siteId;
    session->siteName = displayNameForSiteId(session->siteId);

    if (session->currentUrl.trimmed().isEmpty()) {
        session->currentUrl = defaultUrlForSiteId(session->siteId);
    }

    if (session->title.trimmed().isEmpty()) {
        session->title = session->siteName;
    }

    ensureSessionHasActiveTab(session);
    if (BrowserTab *tab = findTabMutable(session, session->activeTabId)) {
        if (tab->currentUrl.trimmed().isEmpty()) {
            tab->currentUrl = session->currentUrl;
        }
        if (tab->title.trimmed().isEmpty()) {
            tab->title = session->title;
        }
    }
}

BrowserSession *SessionManager::findSessionBySiteMutable(const QString &siteId)
{
    if (siteId.isEmpty()) {
        return nullptr;
    }

    for (BrowserSession &session : m_sessions) {
        if (session.siteId == siteId) {
            return &session;
        }
    }

    return nullptr;
}

const BrowserSession *SessionManager::findSessionBySite(const QString &siteId) const
{
    if (siteId.isEmpty()) {
        return nullptr;
    }

    for (const BrowserSession &session : m_sessions) {
        if (session.siteId == siteId) {
            return &session;
        }
    }

    return nullptr;
}

int SessionManager::matchingBookmarkRowForUrl(const QString &normalizedUrl) const
{
    if (!m_catalog || normalizedUrl.isEmpty()) {
        return -1;
    }

    const QUrl targetUrl(normalizedUrl);
    if (!targetUrl.isValid()) {
        return -1;
    }

    int bestRow = -1;
    int bestScore = -1;
    for (int row = 0; row < m_catalog->count(); ++row) {
        const QString bookmarkUrl = normalizedUrlForStorage(m_catalog->urlAt(row));
        if (bookmarkUrl.isEmpty()) {
            continue;
        }

        const QUrl candidateUrl(bookmarkUrl);
        if (!candidateUrl.isValid() || !bookmarkMatchesUrl(normalizedUrl, targetUrl, bookmarkUrl, candidateUrl)) {
            continue;
        }

        const int score = bookmarkUrl.size();
        if (score > bestScore) {
            bestScore = score;
            bestRow = row;
        }
    }

    return bestRow;
}

int SessionManager::bookmarkRowForSessionKey(const QString &sessionKey) const
{
    if (!m_catalog || sessionKey.isEmpty()) {
        return -1;
    }

    for (int row = 0; row < m_catalog->count(); ++row) {
        if (normalizedUrlForStorage(m_catalog->urlAt(row)) == sessionKey) {
            return row;
        }
    }

    return -1;
}

int SessionManager::indexOfSession(const QString &sessionId) const
{
    for (int index = 0; index < m_sessions.size(); ++index) {
        if (m_sessions.at(index).sessionId == sessionId) {
            return index;
        }
    }

    return -1;
}

QString SessionManager::createSession(const QString &siteId,
                                      const QString &siteName,
                                      const QString &url,
                                      const QString &title,
                                      const QString &type,
                                      const QString &parentSessionId)
{
    if (BrowserSession *existing = findSessionBySiteMutable(siteId)) {
        normalizeSession(existing);
        backfillSessionIconIfNeeded(existing, url);
        return existing->sessionId;
    }

    BrowserSession session;
    session.siteId = siteId;
    session.sessionId = siteId;
    session.siteName = siteName.trimmed().isEmpty() ? displayNameForSiteId(siteId) : siteName.trimmed();
    session.title = title.trimmed().isEmpty() ? session.siteName : title.trimmed();
    session.type = type;
    session.currentUrl = url;
    session.parentSessionId = parentSessionId;
    session.updatedAt = QDateTime::currentDateTimeUtc();

    BrowserTab tab;
    tab.tabId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    tab.title = session.title;
    tab.currentUrl = session.currentUrl;
    tab.updatedAt = session.updatedAt;
    session.activeTabId = tab.tabId;
    session.tabs.append(tab);

    normalizeSession(&session);
    backfillSessionIconIfNeeded(&session, url);

    m_sessions.append(session);
    return session.sessionId;
}

void SessionManager::setActiveSession(const QString &sessionId)
{
    BrowserSession *session = findSessionMutable(sessionId);
    if (!session) {
        return;
    }

    normalizeSession(session);
    session->updatedAt = QDateTime::currentDateTimeUtc();
    ensureSessionHasActiveTab(session);
    m_activeSessionId = session->sessionId;
    emit activeSessionChanged();
    emit sessionsChanged();
}

QList<BrowserSession> SessionManager::orderedSessions() const
{
    QList<BrowserSession> sessions;
    QSet<QString> seenSiteIds;
    for (const BrowserSession &session : m_sessions) {
        if (session.siteId.isEmpty() || seenSiteIds.contains(session.siteId)) {
            continue;
        }

        seenSiteIds.insert(session.siteId);
        sessions.append(session);
    }
    return sessions;
}

QString SessionManager::displayNameForSiteId(const QString &siteId) const
{
    const int bookmarkRow = bookmarkRowForSessionKey(siteId);
    if (bookmarkRow >= 0) {
        const QString title = m_catalog->titleAt(bookmarkRow).trimmed();
        if (!title.isEmpty()) {
            return title;
        }
    }

    const QUrl url(siteId);
    if (url.isValid() && !url.host().trimmed().isEmpty()) {
        return url.host().trimmed().toLower();
    }

    return siteId.trimmed();
}

QString SessionManager::defaultUrlForSiteId(const QString &siteId) const
{
    const int bookmarkRow = bookmarkRowForSessionKey(siteId);
    if (bookmarkRow >= 0) {
        return normalizedUrl(m_catalog->urlAt(bookmarkRow));
    }

    const QUrl parsed(siteId);
    if (parsed.isValid() && !parsed.scheme().isEmpty()) {
        return normalizedUrl(siteId);
    }

    if (!siteId.trimmed().isEmpty()) {
        return normalizedUrl(QStringLiteral("https://%1").arg(siteId.trimmed()));
    }

    return {};
}

QString SessionManager::normalizedUrl(const QString &url) const
{
    return normalizedUrlForStorage(url);
}

QString SessionManager::siteIconForUrl(const QString &url) const
{
    const QString normalized = normalizedUrl(url);
    if (normalized.isEmpty() || !DatabaseManager::isInitialized()) {
        return {};
    }

    const WebsiteRepository repo;
    if (const std::optional<Website> website = repo.findMatchingForUrl(normalized)) {
        return website->icon;
    }
    return {};
}

void SessionManager::backfillSessionIconIfNeeded(BrowserSession *session, const QString &url)
{
    if (!session || !session->icon.isEmpty()) {
        return;
    }

    const QString icon = siteIconForUrl(url);
    if (!icon.isEmpty()) {
        session->icon = icon;
    }
}

void SessionManager::refreshIconForSiteId(const QString &siteId, const QString &icon)
{
    if (siteId.isEmpty() || icon.isEmpty()) {
        return;
    }

    bool changed = false;
    for (BrowserSession &session : m_sessions) {
        if (session.siteId == siteId && session.icon.isEmpty()) {
            session.icon = icon;
            changed = true;
        }
    }

    if (changed) {
        emit sessionsChanged();
    }
}