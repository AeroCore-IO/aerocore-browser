#include "website/websitesessioncatalog.h"
#include "website/websiterepository.h"
#include "database/databasemanager.h"

#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>

#include "test_util.h"

class TestWebsiteSessionCatalog : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void refreshSortsByUrlLengthDescending();
    void urlAtAndTitleAtExposeRepositoryData();

private:
    QTemporaryDir m_tempDir;
    WebsiteRepository m_repository;
    WebsiteSessionCatalog m_catalog;
};

void TestWebsiteSessionCatalog::init()
{
    QVERIFY(m_tempDir.isValid());
    QVERIFY2(QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE")),
             qPrintable(QStringLiteral("Missing QSQLITE driver.")));
    QVERIFY(DatabaseManager::initialize(m_tempDir.path() + QStringLiteral("/test.db")));
    m_catalog.setRepository(&m_repository);
}

void TestWebsiteSessionCatalog::cleanup()
{
    DatabaseManager::close();
}

void TestWebsiteSessionCatalog::refreshSortsByUrlLengthDescending()
{
    QVERIFY(m_repository.add(QStringLiteral("root"), QStringLiteral("https://example.com")));
    QVERIFY(m_repository.add(QStringLiteral("nested"), QStringLiteral("https://example.com/docs")));

    m_catalog.refresh();
    QCOMPARE(m_catalog.count(), 2);
    QCOMPARE(m_catalog.urlAt(0), QStringLiteral("https://example.com/docs"));
    QCOMPARE(m_catalog.urlAt(1), QStringLiteral("https://example.com"));
}

void TestWebsiteSessionCatalog::urlAtAndTitleAtExposeRepositoryData()
{
    QVERIFY(m_repository.add(QStringLiteral("Docs"), QStringLiteral("https://docs.example.com")));

    m_catalog.refresh();
    QCOMPARE(m_catalog.count(), 1);
    QCOMPARE(m_catalog.urlAt(0), QStringLiteral("https://docs.example.com"));
    QCOMPARE(m_catalog.titleAt(0), QStringLiteral("Docs"));
}

QTEST_CORE_MAIN(TestWebsiteSessionCatalog)
#include "tst_websitesessioncatalog.moc"
