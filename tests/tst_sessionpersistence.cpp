#include "sessionmanager.h"
#include "sessionpersistence.h"

#include <QCoreApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "test_util.h"

//
class MockSessionSiteCatalog : public SessionSiteCatalog
{
public:
    void setEntries(QList<QPair<QString, QString>> entries)
    {
        m_entries.clear();
        for (const auto &entry : entries) {
            m_entries.append({entry.first, entry.second});
        }
    }

    int count() const override
    {
        return m_entries.size();
    }

    QString urlAt(int row) const override
    {
        return row >= 0 && row < m_entries.size() ? m_entries.at(row).first : QString();
    }

    QString titleAt(int row) const override
    {
        return row >= 0 && row < m_entries.size() ? m_entries.at(row).second : QString();
    }

private:
    QList<QPair<QString, QString>> m_entries;
};

class TestSessionPersistence : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void roundtripPreservesSessionsAndTabs();
    void loadReturnsEmptyWithoutSavedData();
    void saveReplacesStaleSnapshot();
    void restoreCreatesPlaceholderSessions();
    void restoreSkipsInvalidAndDuplicateSessions();
    void restoreFallsBackToFirstSessionWhenActiveIdUnknown();
    void restoreIsNoOpWhenSessionsAlreadyExist();

private:
    BrowserSession makeSession(const QString &siteId, const QString &url, const QString &title) const;

    QTemporaryDir m_tempDir;
    MockSessionSiteCatalog m_catalog;
    SessionManager *m_manager = nullptr;
};

void TestSessionPersistence::init()
{
    QVERIFY(m_tempDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("AeroCoreTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SessionPersistenceTest"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    {
        QSettings settings;
        settings.remove(QStringLiteral("sessions"));
    }

    m_catalog.setEntries({
        {QStringLiteral("https://example.com"), QStringLiteral("Example")},
    });
    delete m_manager;
    m_manager = new SessionManager(&m_catalog, this);
}

void TestSessionPersistence::cleanup()
{
    delete m_manager;
    m_manager = nullptr;
}

BrowserSession TestSessionPersistence::makeSession(const QString &siteId,
                                                   const QString &url,
                                                   const QString &title) const
{
    BrowserSession session;
    session.siteId = siteId;
    session.sessionId = siteId;
    session.siteName = title;
    session.title = title;
    session.currentUrl = url;
    session.updatedAt = QDateTime::currentDateTimeUtc().addSecs(-60);

    BrowserTab tab;
    tab.tabId = QStringLiteral("tab-") + siteId;
    tab.title = title;
    tab.currentUrl = url;
    tab.updatedAt = session.updatedAt;
    session.activeTabId = tab.tabId;
    session.tabs.append(tab);
    return session;
}

void TestSessionPersistence::roundtripPreservesSessionsAndTabs()
{
    BrowserSession first = makeSession(QStringLiteral("https://a.example.com"),
                                       QStringLiteral("https://a.example.com/page"),
                                       QStringLiteral("Site A"));
    BrowserSession second = makeSession(QStringLiteral("https://b.example.com"),
                                        QStringLiteral("https://b.example.com"),
                                        QStringLiteral("Site B"));
    BrowserTab extraTab;
    extraTab.tabId = QStringLiteral("tab-b2");
    extraTab.title = QStringLiteral("Video");
    extraTab.currentUrl = QStringLiteral("https://b.example.com/video");
    extraTab.updatedAt = second.updatedAt;
    second.tabs.append(extraTab);

    SessionPersistence::save({first, second}, second.sessionId);

    QString activeSessionId;
    const QList<BrowserSession> loaded = SessionPersistence::load(&activeSessionId);
    QCOMPARE(activeSessionId, second.sessionId);
    QCOMPARE(loaded.size(), 2);

    QCOMPARE(loaded.at(0).siteId, first.siteId);
    QCOMPARE(loaded.at(0).title, first.title);
    QCOMPARE(loaded.at(0).currentUrl, first.currentUrl);
    QCOMPARE(loaded.at(0).activeTabId, first.activeTabId);
    QCOMPARE(loaded.at(0).tabs.size(), 1);
    QCOMPARE(loaded.at(0).tabs.constFirst().tabId, QStringLiteral("tab-https://a.example.com"));
    QCOMPARE(loaded.at(0).tabs.constFirst().currentUrl, first.currentUrl);

    QCOMPARE(loaded.at(1).tabs.size(), 2);
    QCOMPARE(loaded.at(1).tabs.at(1).currentUrl, QStringLiteral("https://b.example.com/video"));
    QVERIFY(loaded.at(1).updatedAt.isValid());
}

void TestSessionPersistence::loadReturnsEmptyWithoutSavedData()
{
    QString activeSessionId = QStringLiteral("stale");
    const QList<BrowserSession> loaded = SessionPersistence::load(&activeSessionId);
    QVERIFY(loaded.isEmpty());
    QVERIFY(activeSessionId.isEmpty());
}

void TestSessionPersistence::saveReplacesStaleSnapshot()
{
    SessionPersistence::save({makeSession(QStringLiteral("https://a.example.com"),
                                          QStringLiteral("https://a.example.com"),
                                          QStringLiteral("Site A")),
                              makeSession(QStringLiteral("https://b.example.com"),
                                          QStringLiteral("https://b.example.com"),
                                          QStringLiteral("Site B"))},
                             QStringLiteral("https://b.example.com"));

    SessionPersistence::save({}, QString());

    const QList<BrowserSession> loaded = SessionPersistence::load();
    QVERIFY(loaded.isEmpty());
}

void TestSessionPersistence::restoreCreatesPlaceholderSessions()
{
    BrowserSession session = makeSession(QStringLiteral("https://example.com"),
                                         QStringLiteral("https://example.com/deep/page"),
                                         QStringLiteral("Deep Page"));
    QSignalSpy spy(m_manager, &SessionManager::sessionsChanged);

    m_manager->restoreSessions({session}, session.sessionId);

    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_manager->allSessions().size(), 1);
    QCOMPARE(m_manager->activeSessionId(), session.sessionId);
    QCOMPARE(m_manager->activeSessionUrl(), QStringLiteral("https://example.com/deep/page"));
    const QList<BrowserTab> tabs = m_manager->tabsForSession(session.sessionId);
    QCOMPARE(tabs.size(), 1);
    QCOMPARE(tabs.constFirst().currentUrl, QStringLiteral("https://example.com/deep/page"));
    QCOMPARE(tabs.constFirst().title, QStringLiteral("Deep Page"));
    QCOMPARE(m_manager->activateSession(session.sessionId),
             QStringLiteral("https://example.com/deep/page"));
}

void TestSessionPersistence::restoreSkipsInvalidAndDuplicateSessions()
{
    BrowserSession valid = makeSession(QStringLiteral("https://example.com"),
                                       QStringLiteral("https://example.com"),
                                       QStringLiteral("Example"));
    BrowserSession noUrl = makeSession(QStringLiteral("https://empty.example.com"), QString(),
                                       QStringLiteral("No Url"));
    BrowserSession duplicate = makeSession(QStringLiteral("https://example.com"),
                                           QStringLiteral("https://example.com/other"),
                                           QStringLiteral("Duplicate"));
    BrowserSession tabless = makeSession(QStringLiteral("https://c.example.com"),
                                         QStringLiteral("https://c.example.com"),
                                         QStringLiteral("Site C"));
    tabless.tabs.clear();
    tabless.activeTabId.clear();

    m_manager->restoreSessions({valid, noUrl, duplicate, tabless}, QStringLiteral("unknown-id"));

    QCOMPARE(m_manager->allSessions().size(), 2);
    QCOMPARE(m_manager->tabsForSession(QStringLiteral("https://c.example.com")).size(), 1);
    QCOMPARE(m_manager->activeSessionId(), valid.sessionId);
}

void TestSessionPersistence::restoreFallsBackToFirstSessionWhenActiveIdUnknown()
{
    const BrowserSession first = makeSession(QStringLiteral("https://a.example.com"),
                                             QStringLiteral("https://a.example.com"),
                                             QStringLiteral("Site A"));
    const BrowserSession second = makeSession(QStringLiteral("https://b.example.com"),
                                              QStringLiteral("https://b.example.com"),
                                              QStringLiteral("Site B"));

    m_manager->restoreSessions({first, second}, QStringLiteral("https://gone.example.com"));

    QCOMPARE(m_manager->activeSessionId(), first.sessionId);
}

void TestSessionPersistence::restoreIsNoOpWhenSessionsAlreadyExist()
{
    m_manager->ensureSiteSession(QStringLiteral("https://live.example.com"),
                                 QStringLiteral("Live"));

    m_manager->restoreSessions({makeSession(QStringLiteral("https://example.com"),
                                            QStringLiteral("https://example.com"),
                                            QStringLiteral("Example"))},
                               QStringLiteral("https://example.com"));

    QCOMPARE(m_manager->allSessions().size(), 1);
    QCOMPARE(m_manager->allSessions().constFirst().siteId, QStringLiteral("live.example.com"));
}

QTEST_CORE_MAIN(TestSessionPersistence)
#include "tst_sessionpersistence.moc"
