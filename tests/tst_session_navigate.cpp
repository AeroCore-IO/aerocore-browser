#include "sessionmanager.h"

#include <QSignalSpy>
#include <QTest>

#include "test_util.h"

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

class TestSessionNavigate : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void siteIdForUrlUsesCatalogUrl();
    void activateOrCreateSiteSessionRestoresExistingUrl();

private:
    MockSessionSiteCatalog m_catalog;
    SessionManager *m_manager = nullptr;
};

void TestSessionNavigate::init()
{
    delete m_manager;
    m_manager = new SessionManager(&m_catalog, this);
}

void TestSessionNavigate::cleanup()
{
    delete m_manager;
    m_manager = nullptr;
}

void TestSessionNavigate::siteIdForUrlUsesCatalogUrl()
{
    m_catalog.setEntries({
        {QStringLiteral("https://example.com/docs"), QStringLiteral("Docs")},
        {QStringLiteral("https://example.com"), QStringLiteral("Example")},
    });

    QCOMPARE(m_manager->siteIdForUrl(QStringLiteral("https://example.com/docs/guide")),
             QStringLiteral("https://example.com/docs"));
    QCOMPARE(m_manager->siteNameForUrl(QStringLiteral("https://example.com/docs/guide")),
             QStringLiteral("Docs"));
}

void TestSessionNavigate::activateOrCreateSiteSessionRestoresExistingUrl()
{
    m_catalog.setEntries({
        {QStringLiteral("https://example.com"), QStringLiteral("Example")},
    });

    const QString created = m_manager->activateOrCreateSiteSession(QStringLiteral("https://example.com"));
    QCOMPARE(created, QStringLiteral("https://example.com"));

    m_manager->updateActiveSession(QStringLiteral("https://example.com/page"), QStringLiteral("Page"));

    QSignalSpy spy(m_manager, &SessionManager::sessionsChanged);
    const QString resumed = m_manager->activateOrCreateSiteSession(QStringLiteral("https://example.com"));
    QCOMPARE(resumed, QStringLiteral("https://example.com/page"));
    QCOMPARE(spy.count(), 0);
}

QTEST_CORE_MAIN(TestSessionNavigate)
#include "tst_session_navigate.moc"
