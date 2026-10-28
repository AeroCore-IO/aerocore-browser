#include "maincontroller.h"
#include "database/databasemanager.h"
#include "history/historyrepository.h"
#include "history/historythumbnailstorage.h"

#include <QImage>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QUrl>

#include "test_util.h"

class TestMainControllerHistory : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void getHistoryListReturnsEmptyArrayWhenDatabaseUninitialized();
    void getHistoryListReturnsSeededItems();
    void deleteHistorySuccess();
    void deleteHistoryRemovesThumbnailFile();
    void deleteHistoryFailureStillEmitsSignal();
    void deleteHistoryRejectsInvalidId();
    void clearAllHistoryRemovesThumbnailFiles();
    void recordHistoryVisitRevisitDefersHistoryListChange();
    void updateHistoryThumbnailSetsThumbnailUrl();
    void updateHistoryThumbnailRevisitKeepsUrlAndUpdatesTimestamp();
    void openHistoryUrlCreatesNewTabWhenSiteSessionExists();

private:
    QTemporaryDir m_tempDir;
    MainController *m_controller = nullptr;
    HistoryRepository m_historyRepository;

    QJsonObject firstHistoryItem() const
    {
        const QJsonArray items = QJsonDocument::fromJson(m_controller->getHistoryList().toUtf8())
                                     .array().first().toObject().value(QStringLiteral("items")).toArray();
        return items.first().toObject();
    }
};

void TestMainControllerHistory::init()
{
    QVERIFY(m_tempDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("AeroCoreTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MainControllerHistoryTest"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    {
        QSettings settings;
        settings.remove(QStringLiteral("sessions"));
    }
    QVERIFY2(QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE")),
             qPrintable(QStringLiteral("Missing QSQLITE driver. Available: ")
                        + QSqlDatabase::drivers().join(QStringLiteral(", "))));

    m_controller = new MainController();
    QVERIFY2(m_controller->initializeWebsiteDatabase(m_tempDir.path() + QStringLiteral("/test.db")),
             "Failed to initialize website database");
    QVERIFY(DatabaseManager::isInitialized());
    HistoryThumbnailStorage::setThumbnailsStorageDirectory(
        m_tempDir.path() + QStringLiteral("/history-thumbnails"));

    QVERIFY(m_controller->addWebsite(QStringLiteral("Example"), QStringLiteral("https://example.com")).isEmpty());
    QVERIFY(m_historyRepository.upsertVisit(QStringLiteral("Example Page"),
                                            QStringLiteral("https://example.com/page")));
}

void TestMainControllerHistory::cleanup()
{
    delete m_controller;
    m_controller = nullptr;
    HistoryThumbnailStorage::setThumbnailsStorageDirectory({});
    DatabaseManager::close();
}

void TestMainControllerHistory::getHistoryListReturnsEmptyArrayWhenDatabaseUninitialized()
{
    DatabaseManager::close();
    MainController controller;
    QCOMPARE(controller.getHistoryList(), QStringLiteral("[]"));
}

void TestMainControllerHistory::getHistoryListReturnsSeededItems()
{
    const QJsonDocument doc = QJsonDocument::fromJson(m_controller->getHistoryList().toUtf8());
    const QJsonArray sites = doc.array();
    QCOMPARE(sites.size(), 1);

    const QJsonObject site = sites.first().toObject();
    QCOMPARE(site.value(QStringLiteral("name")).toString(), QStringLiteral("Example"));
    const QJsonArray items = site.value(QStringLiteral("items")).toArray();
    QCOMPARE(items.size(), 1);

    const QJsonObject item = items.first().toObject();
    QCOMPARE(item.value(QStringLiteral("title")).toString(), QStringLiteral("Example Page"));
    QCOMPARE(item.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.com/page"));
    QCOMPARE(item.value(QStringLiteral("visit_count")).toInt(), 1);
    QVERIFY(item.contains(QStringLiteral("id")));
    QVERIFY(item.contains(QStringLiteral("website_id")));
    QVERIFY(item.contains(QStringLiteral("website_name")));
    QVERIFY(item.contains(QStringLiteral("thumbnail")));
    QVERIFY(item.contains(QStringLiteral("last_visit_time")));
    QVERIFY(item.contains(QStringLiteral("created_at")));
    QVERIFY(item.contains(QStringLiteral("updated_at")));
}

void TestMainControllerHistory::deleteHistorySuccess()
{
    const QJsonArray items = QJsonDocument::fromJson(m_controller->getHistoryList().toUtf8())
                                   .array().first().toObject().value(QStringLiteral("items")).toArray();
    const QString id = items.first().toObject().value(QStringLiteral("id")).toString();

    QSignalSpy spy(m_controller, &MainController::historyListChange);
    QVERIFY(m_controller->deleteHistory(id));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_controller->getHistoryList(), QStringLiteral("[]"));
}

void TestMainControllerHistory::deleteHistoryRemovesThumbnailFile()
{
    QImage image(200, 150, QImage::Format_ARGB32);
    image.fill(Qt::red);
    m_controller->updateHistoryThumbnail(QStringLiteral("https://example.com/page"), image);

    const QJsonObject item = firstHistoryItem();
    const QString id = item.value(QStringLiteral("id")).toString();
    const QString localPath = QUrl(item.value(QStringLiteral("thumbnail")).toString()).toLocalFile();
    QVERIFY(QFile::exists(localPath));

    QVERIFY(m_controller->deleteHistory(id));
    QVERIFY(!QFile::exists(localPath));
}

void TestMainControllerHistory::deleteHistoryFailureStillEmitsSignal()
{
    QSignalSpy spy(m_controller, &MainController::historyListChange);
    QVERIFY(!m_controller->deleteHistory(QStringLiteral("missing-id")));
    QCOMPARE(spy.count(), 1);
}

void TestMainControllerHistory::deleteHistoryRejectsInvalidId()
{
    QSignalSpy spy(m_controller, &MainController::historyListChange);
    QVERIFY(!m_controller->deleteHistory(QString()));
    QCOMPARE(spy.count(), 1);
}

void TestMainControllerHistory::clearAllHistoryRemovesThumbnailFiles()
{
    QImage image(200, 150, QImage::Format_ARGB32);
    image.fill(Qt::red);
    m_controller->updateHistoryThumbnail(QStringLiteral("https://example.com/page"), image);

    const QString localPath = QUrl(firstHistoryItem().value(QStringLiteral("thumbnail")).toString()).toLocalFile();
    QVERIFY(QFile::exists(localPath));

    QSignalSpy spy(m_controller, &MainController::historyListChange);
    m_controller->clearAllHistory();
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_controller->getHistoryList(), QStringLiteral("[]"));
    QVERIFY(!QFile::exists(localPath));
}

void TestMainControllerHistory::recordHistoryVisitRevisitDefersHistoryListChange()
{
    QImage image(200, 150, QImage::Format_ARGB32);
    image.fill(Qt::red);
    m_controller->updateHistoryThumbnail(QStringLiteral("https://example.com/page"), image);

    QSignalSpy spy(m_controller, &MainController::historyListChange);
    m_controller->recordHistoryVisit(QStringLiteral("Example Page"),
                                     QStringLiteral("https://example.com/page"));
    QCOMPARE(spy.count(), 0);

    m_controller->completeHistoryVisitUiUpdate(QStringLiteral("https://example.com/page"));
    QCOMPARE(spy.count(), 1);
}

void TestMainControllerHistory::updateHistoryThumbnailSetsThumbnailUrl()
{
    QImage image(200, 150, QImage::Format_ARGB32);
    image.fill(Qt::red);

    QSignalSpy spy(m_controller, &MainController::historyListChange);
    m_controller->updateHistoryThumbnail(QStringLiteral("https://example.com/page"), image);
    QCOMPARE(spy.count(), 1);

    const QJsonObject item = firstHistoryItem();
    QVERIFY(!item.value(QStringLiteral("thumbnail")).toString().isEmpty());
    QVERIFY(item.value(QStringLiteral("thumbnail")).toString().startsWith(QStringLiteral("file:")));
}

void TestMainControllerHistory::updateHistoryThumbnailRevisitKeepsUrlAndUpdatesTimestamp()
{
    QImage firstImage(200, 150, QImage::Format_ARGB32);
    firstImage.fill(Qt::red);
    m_controller->updateHistoryThumbnail(QStringLiteral("https://example.com/page"), firstImage);

    const QJsonObject firstItem = firstHistoryItem();
    const QString firstThumbnail = firstItem.value(QStringLiteral("thumbnail")).toString();
    const qint64 firstUpdatedAt = firstItem.value(QStringLiteral("updated_at")).toInteger();
    QVERIFY(!firstThumbnail.isEmpty());

    QThread::msleep(5);
    QImage secondImage(200, 150, QImage::Format_ARGB32);
    secondImage.fill(Qt::blue);

    QSignalSpy spy(m_controller, &MainController::historyListChange);
    m_controller->updateHistoryThumbnail(QStringLiteral("https://example.com/page"), secondImage);
    QCOMPARE(spy.count(), 1);

    const QJsonObject secondItem = firstHistoryItem();
    QCOMPARE(secondItem.value(QStringLiteral("thumbnail")).toString(), firstThumbnail);
    QVERIFY(secondItem.value(QStringLiteral("updated_at")).toInteger() >= firstUpdatedAt);
}

void TestMainControllerHistory::openHistoryUrlCreatesNewTabWhenSiteSessionExists()
{
    m_controller->initializeSessions();
    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));
    m_controller->onActiveBrowserUrlChanged(QStringLiteral("https://example.com/page"),
                                            QStringLiteral("Page"));

    QObject containerMarker;
    m_controller->bindBrowserContainer(&containerMarker);

    const QString historyUrl = QStringLiteral("https://example.com/history-item");
    QCOMPARE(m_controller->openHistoryUrl(historyUrl), historyUrl);

    const QJsonArray sessions = QJsonDocument::fromJson(
                                    m_controller->getSessionListJson().toUtf8())
                                    .array();
    QCOMPARE(sessions.size(), 1);
    const QString sessionId = sessions.first().toObject().value(QStringLiteral("sessionId")).toString();
    const QJsonArray tabs = QJsonDocument::fromJson(
                                m_controller->getTabListJson(sessionId).toUtf8())
                                .array();
    QCOMPARE(tabs.size(), 2);

    QStringList urls;
    QString activeUrl;
    for (const QJsonValue &value : tabs) {
        const QJsonObject tab = value.toObject();
        const QString url = tab.value(QStringLiteral("url")).toString();
        urls.append(url);
        if (tab.value(QStringLiteral("isActive")).toBool()) {
            activeUrl = url;
        }
    }
    QVERIFY(urls.contains(QStringLiteral("https://example.com/page")));
    QVERIFY(urls.contains(historyUrl));
    QCOMPARE(activeUrl, historyUrl);
}

QTEST_CORE_MAIN(TestMainControllerHistory)
#include "tst_maincontroller_history.moc"
