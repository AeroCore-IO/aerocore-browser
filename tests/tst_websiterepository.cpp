#include "database/databasemanager.h"
#include "website/websiteiconstorage.h"
#include "website/websitepresets.h"
#include "website/websiterepository.h"

#include <QTemporaryDir>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QTest>
#include <QUrl>

#include "test_util.h"

class TestWebsiteRepository : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void addAndList();
    void timestampsOnAdd();
    void updateRefreshesUpdatedAt();
    void removeWebsite();
    void removeMissingReturnsFalse();
    void updateMissingReturnsFalse();
    void duplicateUrlRejected();
    void duplicateNameAllowed();
    void urlExistsExcludesSelf();
    void listAllSortedByCreatedAt();
    void importPresetsIfEmpty();
    void importPresetsSkipsWhenNotEmpty();
    void findByUrlReturnsExactMatch();
    void updateIconIfEmptyOnlyWhenIconMissing();

private:
    QTemporaryDir m_tempDir;
    WebsiteRepository m_repository;
};

void TestWebsiteRepository::init()
{
    QVERIFY(m_tempDir.isValid());
    WebsiteIconStorage::setPresetIconSourceDirectory(
        QString(AEROCORE_SOURCE_DIR) + QStringLiteral("/assets/website-icons"));
    WebsiteIconStorage::setIconsStorageDirectory(m_tempDir.path() + QStringLiteral("/website-icons"));
    QVERIFY2(QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE")),
             qPrintable(QStringLiteral("Missing QSQLITE driver. Available: ")
                        + QSqlDatabase::drivers().join(QStringLiteral(", "))));

    const QString dbPath = m_tempDir.path() + QStringLiteral("/test.db");
    QVERIFY2(DatabaseManager::initialize(dbPath), "DatabaseManager::initialize failed");
    QVERIFY2(DatabaseManager::isInitialized(), "Database is not open after initialize");

    QSqlQuery probe(DatabaseManager::database());
    QVERIFY2(probe.exec(QStringLiteral("SELECT COUNT(*) FROM websites")),
             qPrintable(probe.lastError().text()));
}

void TestWebsiteRepository::cleanup()
{
    WebsiteIconStorage::setPresetIconSourceDirectory({});
    WebsiteIconStorage::setIconsStorageDirectory({});
    DatabaseManager::close();
}

void TestWebsiteRepository::addAndList()
{
    QVERIFY2(DatabaseManager::isInitialized(), "database closed before addAndList");
    QVERIFY2(m_repository.add(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")),
             qPrintable(WebsiteRepository::lastSqlError()));
    const QList<Website> websites = m_repository.listAll();
    QCOMPARE(websites.size(), 1);
    QCOMPARE(websites.first().name, QStringLiteral("site-a"));
    QCOMPARE(websites.first().url, QStringLiteral("https://a.example.com"));
    QVERIFY(!websites.first().id.isEmpty());
}

void TestWebsiteRepository::timestampsOnAdd()
{
    QVERIFY(m_repository.add(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")));
    const Website website = m_repository.listAll().first();
    QVERIFY(website.createdAt.isValid());
    QVERIFY(website.updatedAt.isValid());
    QCOMPARE(website.createdAt, website.updatedAt);
}

void TestWebsiteRepository::updateRefreshesUpdatedAt()
{
    QVERIFY(m_repository.add(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")));
    Website website = m_repository.listAll().first();
    const QDateTime createdAt = website.createdAt;
    const QDateTime updatedAt = website.updatedAt;

    website.name = QStringLiteral("site-b");
    QVERIFY(m_repository.update(website));

    const Website updated = m_repository.findById(website.id).value();
    QCOMPARE(updated.createdAt, createdAt);
    QVERIFY(updated.updatedAt >= updatedAt);
    QCOMPARE(updated.name, QStringLiteral("site-b"));
}

void TestWebsiteRepository::removeWebsite()
{
    QVERIFY(m_repository.add(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")));
    const QString id = m_repository.listAll().first().id;
    QVERIFY(m_repository.remove(id));
    QVERIFY(m_repository.listAll().isEmpty());
}

void TestWebsiteRepository::removeMissingReturnsFalse()
{
    QVERIFY(!m_repository.remove(QStringLiteral("missing-id")));
}

void TestWebsiteRepository::updateMissingReturnsFalse()
{
    Website website;
    website.id = QStringLiteral("missing-id");
    website.name = QStringLiteral("site");
    website.url = QStringLiteral("https://example.com");
    QVERIFY(!m_repository.update(website));
}

void TestWebsiteRepository::duplicateUrlRejected()
{
    QVERIFY(m_repository.add(QStringLiteral("site-a"), QStringLiteral("https://example.com")));
    QVERIFY(!m_repository.add(QStringLiteral("site-b"), QStringLiteral("https://example.com")));
}

void TestWebsiteRepository::duplicateNameAllowed()
{
    QVERIFY(m_repository.add(QStringLiteral("same-name"), QStringLiteral("https://a.example.com")));
    QVERIFY(m_repository.add(QStringLiteral("same-name"), QStringLiteral("https://b.example.com")));
    QCOMPARE(m_repository.listAll().size(), 2);
}

void TestWebsiteRepository::urlExistsExcludesSelf()
{
    QVERIFY(m_repository.add(QStringLiteral("site-a"), QStringLiteral("https://example.com")));
    const QString id = m_repository.listAll().first().id;
    QVERIFY(m_repository.urlExists(QStringLiteral("https://example.com")));
    QVERIFY(!m_repository.urlExists(QStringLiteral("https://example.com"), id));
}

void TestWebsiteRepository::listAllSortedByCreatedAt()
{
    QVERIFY(m_repository.add(QStringLiteral("first"), QStringLiteral("https://first.example.com")));
    QVERIFY(m_repository.add(QStringLiteral("second"), QStringLiteral("https://second.example.com")));
    const QList<Website> websites = m_repository.listAll();
    QCOMPARE(websites.size(), 2);
    QVERIFY(websites.first().createdAt <= websites.last().createdAt);
    QCOMPARE(websites.first().name, QStringLiteral("first"));
}

void TestWebsiteRepository::importPresetsIfEmpty()
{
    QCOMPARE(m_repository.importPresetsIfEmpty(), builtInPresetWebsites().size());
    const QList<Website> websites = m_repository.listAll();
    QCOMPARE(websites.size(), builtInPresetWebsites().size());
    QCOMPARE(websites.first().name, QStringLiteral("哔哩哔哩"));
    QCOMPARE(websites.first().url, QStringLiteral("https://www.bilibili.com"));
    QVERIFY(websites.first().icon.startsWith(QStringLiteral("file:")));
    QCOMPARE(QUrl(websites.first().icon).fileName(), QStringLiteral("bilibili.svg"));
}

void TestWebsiteRepository::importPresetsSkipsWhenNotEmpty()
{
    QVERIFY(m_repository.add(QStringLiteral("custom"), QStringLiteral("https://custom.example.com")));
    QCOMPARE(m_repository.importPresetsIfEmpty(), 0);
    QCOMPARE(m_repository.listAll().size(), 1);
}

void TestWebsiteRepository::findByUrlReturnsExactMatch()
{
    QVERIFY(m_repository.add(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")));
    const std::optional<Website> website = m_repository.findByUrl(QStringLiteral("https://a.example.com"));
    QVERIFY(website.has_value());
    QCOMPARE(website->name, QStringLiteral("site-a"));
    QVERIFY(!m_repository.findByUrl(QStringLiteral("https://missing.example.com")).has_value());
}

void TestWebsiteRepository::updateIconIfEmptyOnlyWhenIconMissing()
{
    QVERIFY(m_repository.add(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")));
    const QString id = m_repository.listAll().first().id;
    const QString iconUrl = QStringLiteral("file:///tmp/test-icon.png");

    QVERIFY(m_repository.updateIconIfEmpty(id, iconUrl));
    const Website updated = m_repository.findById(id).value();
    QCOMPARE(updated.icon, iconUrl);

    QVERIFY(!m_repository.updateIconIfEmpty(id, QStringLiteral("file:///tmp/other.png")));
    QCOMPARE(m_repository.findById(id).value().icon, iconUrl);
}

QTEST_CORE_MAIN(TestWebsiteRepository)
#include "tst_websiterepository.moc"
