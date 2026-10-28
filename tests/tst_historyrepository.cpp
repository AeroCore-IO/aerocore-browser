#include "database/databasemanager.h"
#include "history/historyrepository.h"
#include "website/websiterepository.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QTest>
#include <QThread>

#include "test_util.h"

class TestHistoryRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void upsertInsertsNewItem();
    void upsertUpdatesVisitCount();
    void prefixMatchAssociatesWebsite();
    void upsertRejectsUrlWithoutMatchingWebsite();
    void removeAndExists();
    void clearAllRemovesAllItems();
    void clearAllOnEmptyStore();
    void listAllSortedByLastVisitTime();
    void setThumbnailIfEmptyWritesOnce();
    void touchUpdatedAtOnlyUpdatesTimestamp();
    void idForUrlReturnsHistoryId();
    void removeAssociatedWithWebsiteDeletesLinkedHistory();
    void removeAssociatedWithWebsiteKeepsUnrelatedHistory();

private:
    QTemporaryDir m_tempDir;
    HistoryRepository m_historyRepository;
    WebsiteRepository m_websiteRepository;
};

void TestHistoryRepository::init()
{
    QVERIFY(m_tempDir.isValid());
    QVERIFY2(QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE")),
             qPrintable(QStringLiteral("Missing QSQLITE driver. Available: ")
                        + QSqlDatabase::drivers().join(QStringLiteral(", "))));

    const QString dbPath = m_tempDir.path() + QStringLiteral("/test.db");
    QVERIFY2(DatabaseManager::initialize(dbPath), "DatabaseManager::initialize failed");
    QVERIFY(DatabaseManager::isInitialized());
}

void TestHistoryRepository::cleanup()
{
    DatabaseManager::close();
}

void TestHistoryRepository::upsertInsertsNewItem()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    const QString websiteId = m_websiteRepository.listAll().first().id;

    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Example"), QStringLiteral("https://example.com/page")));
    const QList<BrowsingHistoryItem> items = m_historyRepository.listAll();
    QCOMPARE(items.size(), 1);
    QCOMPARE(items.first().title, QStringLiteral("Example"));
    QCOMPARE(items.first().url, QStringLiteral("https://example.com/page"));
    QCOMPARE(items.first().visitCount, 1);
    QCOMPARE(items.first().websiteId, websiteId);

    const QJsonObject json = items.first().toJsonObject();
    QCOMPARE(json.value(QStringLiteral("website_name")).toString(), QStringLiteral("Example"));
    QVERIFY(json.contains(QStringLiteral("last_visit_time")));
    QVERIFY(json.contains(QStringLiteral("created_at")));
    QVERIFY(json.contains(QStringLiteral("updated_at")));
}

void TestHistoryRepository::upsertUpdatesVisitCount()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("First"), QStringLiteral("https://example.com")));
    const QString id = m_historyRepository.listAll().first().id;
    const QDateTime createdAt = m_historyRepository.listAll().first().createdAt;

    QThread::msleep(5);
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Second"), QStringLiteral("https://example.com")));

    const BrowsingHistoryItem updated = m_historyRepository.listAll().first();
    QCOMPARE(updated.id, id);
    QCOMPARE(updated.createdAt, createdAt);
    QCOMPARE(updated.title, QStringLiteral("Second"));
    QCOMPARE(updated.visitCount, 2);
    QVERIFY(updated.lastVisitTime >= updated.createdAt);
    QVERIFY(updated.updatedAt >= updated.createdAt);
}

void TestHistoryRepository::prefixMatchAssociatesWebsite()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    const QString websiteId = m_websiteRepository.listAll().first().id;

    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Sub page"),
                                            QStringLiteral("https://example.com/docs/guide")));

    const BrowsingHistoryItem item = m_historyRepository.listAll().first();
    QCOMPARE(item.websiteId, websiteId);
    QCOMPARE(item.websiteName, QStringLiteral("Example"));
}

void TestHistoryRepository::upsertRejectsUrlWithoutMatchingWebsite()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com/home")));
    QVERIFY(!m_historyRepository.upsertVisit(QStringLiteral("Other"),
                                             QStringLiteral("https://example.com/other")));
    QVERIFY(m_historyRepository.listAll().isEmpty());

    QVERIFY(!m_historyRepository.upsertVisit(QStringLiteral("Orphan"),
                                             QStringLiteral("https://orphan.example.com")));
    QVERIFY(m_historyRepository.listAll().isEmpty());
}

void TestHistoryRepository::removeAndExists()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    const QString id = m_historyRepository.listAll().first().id;

    QVERIFY(m_historyRepository.exists(id));
    QVERIFY(m_historyRepository.remove(id));
    QVERIFY(!m_historyRepository.exists(id));
    QVERIFY(!m_historyRepository.remove(id));
}

void TestHistoryRepository::clearAllRemovesAllItems()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("One"), QStringLiteral("https://one.example.com")));
    QVERIFY(m_websiteRepository.add(QStringLiteral("Two"), QStringLiteral("https://two.example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("One"), QStringLiteral("https://one.example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Two"), QStringLiteral("https://two.example.com")));
    QVERIFY(m_historyRepository.clearAll());
    QVERIFY(m_historyRepository.listAll().isEmpty());
}

void TestHistoryRepository::clearAllOnEmptyStore()
{
    QVERIFY(m_historyRepository.clearAll());
    QVERIFY(m_historyRepository.listAll().isEmpty());
}

void TestHistoryRepository::listAllSortedByLastVisitTime()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Older"), QStringLiteral("https://older.example.com")));
    QVERIFY(m_websiteRepository.add(QStringLiteral("Newer"), QStringLiteral("https://newer.example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Older"), QStringLiteral("https://older.example.com")));
    QThread::msleep(1100);
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Newer"), QStringLiteral("https://newer.example.com")));

    const QList<BrowsingHistoryItem> items = m_historyRepository.listAll();
    QCOMPARE(items.size(), 2);
    QVERIFY(items.first().lastVisitTime >= items.last().lastVisitTime);
    QCOMPARE(items.first().url, QStringLiteral("https://newer.example.com"));
    QCOMPARE(items.last().url, QStringLiteral("https://older.example.com"));
}

void TestHistoryRepository::setThumbnailIfEmptyWritesOnce()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    const QString id = m_historyRepository.listAll().first().id;
    const QString thumbnailUrl = QStringLiteral("file:///tmp/example.png");

    QVERIFY(m_historyRepository.setThumbnailIfEmpty(id, thumbnailUrl));
    QCOMPARE(m_historyRepository.listAll().first().thumbnail, thumbnailUrl);
    QVERIFY(!m_historyRepository.setThumbnailIfEmpty(id, QStringLiteral("file:///tmp/other.png")));
    QCOMPARE(m_historyRepository.listAll().first().thumbnail, thumbnailUrl);
}

void TestHistoryRepository::touchUpdatedAtOnlyUpdatesTimestamp()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    const BrowsingHistoryItem before = m_historyRepository.listAll().first();
    const QString id = before.id;
    const QString thumbnailUrl = QStringLiteral("file:///tmp/thumb.png");
    QVERIFY(m_historyRepository.setThumbnailIfEmpty(id, thumbnailUrl));

    QThread::msleep(5);
    QVERIFY(m_historyRepository.touchUpdatedAt(id));

    const BrowsingHistoryItem after = m_historyRepository.listAll().first();
    QCOMPARE(after.thumbnail, thumbnailUrl);
    QVERIFY(after.updatedAt >= before.updatedAt);
}

void TestHistoryRepository::idForUrlReturnsHistoryId()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Example"), QStringLiteral("https://example.com/page")));
    const QString id = m_historyRepository.listAll().first().id;

    const std::optional<QString> foundId = m_historyRepository.idForUrl(QStringLiteral("https://example.com/page"));
    QVERIFY(foundId.has_value());
    QCOMPARE(*foundId, id);
}

void TestHistoryRepository::removeAssociatedWithWebsiteDeletesLinkedHistory()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));
    const QString websiteId = m_websiteRepository.listAll().first().id;
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Docs"),
                                            QStringLiteral("https://example.com/docs/guide")));
    QCOMPARE(m_historyRepository.listAll().first().websiteId, websiteId);

    const QList<BrowsingHistoryItem> removed =
        m_historyRepository.removeAssociatedWithWebsite(websiteId, m_websiteRepository);
    QCOMPARE(removed.size(), 1);
    QVERIFY(m_historyRepository.listAll().isEmpty());
}

void TestHistoryRepository::removeAssociatedWithWebsiteKeepsUnrelatedHistory()
{
    QVERIFY(m_websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com/home")));
    const QString websiteId = m_websiteRepository.listAll().first().id;
    QVERIFY(m_websiteRepository.add(QStringLiteral("Other site"), QStringLiteral("https://other.example.com")));
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Unrelated"),
                                            QStringLiteral("https://other.example.com/page")));

    const QList<BrowsingHistoryItem> removed =
        m_historyRepository.removeAssociatedWithWebsite(websiteId, m_websiteRepository);
    QCOMPARE(removed.size(), 0);
    QCOMPARE(m_historyRepository.listAll().size(), 1);
}

QTEST_CORE_MAIN(TestHistoryRepository)
#include "tst_historyrepository.moc"
