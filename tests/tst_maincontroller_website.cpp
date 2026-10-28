#include "maincontroller.h"
#include "database/databasemanager.h"
#include "website/websiteiconstorage.h"
#include "website/websitepresets.h"

#include <QHostAddress>
#include <QBuffer>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTest>

#include "test_util.h"

class TestMainControllerWebsite : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void addWebsiteRejectsInvalidParams();
    void addWebsiteSuccessEmitsSignal();
    void duplicateUrlRejected();
    void addWebsiteRejectsPresetDuplicateWithMessage();
    void duplicateNameAllowed();
    void deleteWebsiteFailureStillEmitsSignal();
    void deleteWebsiteSuccess();
    void editWebsiteRejectsDuplicateUrl();
    void setWebsiteHasOpenReflectsSessionState();
    void editWebsiteHasOpenFieldIsIgnored();
    void deleteWebsiteClearsOpenSession();
    void deleteWebsiteRemovesAssociatedHistory();
    void editWebsiteUrlChangeClosesSession();
    void runImportsPresetsWhenDatabaseEmpty();
    void sessionListReflectsOpenSite();
    void tabListReturnsTabsForSession();
    void moveSessionWrapsAround();
    void moveTabWrapsAround();
    void confirmSelectionOpensSession();
    void activeTabCountReflectsSession();
    void addWebsiteFetchesIconAsync();
    void temporarySessionFetchesIconAsync();

private:
    QTemporaryDir m_tempDir;
    MainController *m_controller = nullptr;
};

void TestMainControllerWebsite::init()
{
    QVERIFY(m_tempDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("AeroCoreTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MainControllerWebsiteTest"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    {
        QSettings settings;
        settings.remove(QStringLiteral("sessions"));
    }

    WebsiteIconStorage::setPresetIconSourceDirectory(
        QString(AEROCORE_SOURCE_DIR) + QStringLiteral("/assets/website-icons"));
    WebsiteIconStorage::setIconsStorageDirectory(m_tempDir.path() + QStringLiteral("/website-icons"));
    QVERIFY2(QSqlDatabase::drivers().contains(QStringLiteral("QSQLITE")),
             qPrintable(QStringLiteral("Missing QSQLITE driver. Available: ")
                        + QSqlDatabase::drivers().join(QStringLiteral(", "))));

    m_controller = new MainController();
    QVERIFY2(m_controller->initializeWebsiteDatabase(m_tempDir.path() + QStringLiteral("/test.db")),
             "Failed to initialize website database");
    QVERIFY(DatabaseManager::isInitialized());
    m_controller->initializeSessions();
}

void TestMainControllerWebsite::cleanup()
{
    delete m_controller;
    m_controller = nullptr;
    WebsiteIconStorage::setPresetIconSourceDirectory({});
    WebsiteIconStorage::setIconsStorageDirectory({});
    DatabaseManager::close();
}

void TestMainControllerWebsite::addWebsiteRejectsInvalidParams()
{
    QSignalSpy spy(m_controller, &MainController::websiteListChange);
    QCOMPARE(m_controller->addWebsite(QString(), QStringLiteral("https://example.com")),
             QStringLiteral("网站名称不能为空"));
    QCOMPARE(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("hello")),
             QStringLiteral("请输入有效的网址"));
    QCOMPARE(spy.count(), 0);
    QCOMPARE(m_controller->getWebsiteList(), QStringLiteral("[]"));
}

void TestMainControllerWebsite::addWebsiteSuccessEmitsSignal()
{
    QSignalSpy spy(m_controller, &MainController::websiteListChange);
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());

    QCOMPARE(spy.count(), 1);
    const QJsonDocument doc = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8());
    const QJsonArray array = doc.array();
    QCOMPARE(array.size(), 1);
    QCOMPARE(array.first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("site"));
}

void TestMainControllerWebsite::duplicateUrlRejected()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site-a"), QStringLiteral("https://example.com")).isEmpty());
    QCOMPARE(m_controller->addWebsite(QStringLiteral("site-b"), QStringLiteral("https://example.com")),
             QStringLiteral("该网站已存在"));
}

void TestMainControllerWebsite::addWebsiteRejectsPresetDuplicateWithMessage()
{
    m_controller->run();
    QCOMPARE(m_controller->addWebsite(QStringLiteral("B站"), QStringLiteral("www.bilibili.com")),
             QStringLiteral("该网站已存在"));
}

void TestMainControllerWebsite::duplicateNameAllowed()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("same"), QStringLiteral("https://a.example.com")).isEmpty());
    QVERIFY(m_controller->addWebsite(QStringLiteral("same"), QStringLiteral("https://b.example.com")).isEmpty());

    const QJsonDocument doc = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8());
    QCOMPARE(doc.array().size(), 2);
}

void TestMainControllerWebsite::deleteWebsiteFailureStillEmitsSignal()
{
    QSignalSpy spy(m_controller, &MainController::websiteListChange);
    QVERIFY(!m_controller->deleteWebsite(QStringLiteral("missing-id")));
    QCOMPARE(spy.count(), 1);
}

void TestMainControllerWebsite::deleteWebsiteSuccess()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    const QJsonDocument doc = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8());
    const QString id = doc.array().first().toObject().value(QStringLiteral("id")).toString();

    QSignalSpy spy(m_controller, &MainController::websiteListChange);
    QVERIFY(m_controller->deleteWebsite(id));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(m_controller->getWebsiteList(), QStringLiteral("[]"));
}

void TestMainControllerWebsite::editWebsiteRejectsDuplicateUrl()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")).isEmpty());
    QVERIFY(m_controller->addWebsite(QStringLiteral("site-b"), QStringLiteral("https://b.example.com")).isEmpty());

    const QJsonDocument doc = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8());
    const QString idB = doc.array().at(1).toObject().value(QStringLiteral("id")).toString();

    const QString json = QStringLiteral("{\"id\":\"%1\",\"url\":\"https://a.example.com\"}").arg(idB);
    QVERIFY(!m_controller->editWebsite(json));
}

void TestMainControllerWebsite::setWebsiteHasOpenReflectsSessionState()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    const QJsonDocument doc = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8());
    const QString id = doc.array().first().toObject().value(QStringLiteral("id")).toString();

    QVERIFY(!m_controller->setWebsiteHasOpen(QStringLiteral("missing"), true));

    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));

    const QJsonObject item = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8())
                                  .array().first().toObject();
    QVERIFY(item.value(QStringLiteral("has_open")).toBool());
    QVERIFY(m_controller->setWebsiteHasOpen(id, false));
    const QJsonObject afterNoop = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8())
                                       .array().first().toObject();
    QVERIFY(afterNoop.value(QStringLiteral("has_open")).toBool());
}

void TestMainControllerWebsite::editWebsiteHasOpenFieldIsIgnored()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    const QString id = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8())
                           .array().first().toObject().value(QStringLiteral("id")).toString();

    const QString json = QStringLiteral("{\"id\":\"%1\",\"has_open\":true}").arg(id);
    QVERIFY(m_controller->editWebsite(json));

    const QJsonObject item = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8())
                                  .array().first().toObject();
    QVERIFY(!item.value(QStringLiteral("has_open")).toBool());
}

void TestMainControllerWebsite::deleteWebsiteClearsOpenSession()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    const QJsonDocument doc = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8());
    const QString id = doc.array().first().toObject().value(QStringLiteral("id")).toString();

    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));
    QVERIFY(QJsonDocument::fromJson(m_controller->getSessionListJson().toUtf8())
                .array()
                .size()
            > 0);

    QVERIFY(m_controller->deleteWebsite(id));
    QCOMPARE(m_controller->getSessionListJson(), QStringLiteral("[]"));
}

void TestMainControllerWebsite::deleteWebsiteRemovesAssociatedHistory()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    const QString id = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8())
                           .array().first().toObject().value(QStringLiteral("id")).toString();

    m_controller->recordHistoryVisit(QStringLiteral("Page"), QStringLiteral("https://example.com/page"));
    QVERIFY(!m_controller->getHistoryList().isEmpty());

    QSignalSpy websiteSpy(m_controller, &MainController::websiteListChange);
    QSignalSpy historySpy(m_controller, &MainController::historyListChange);
    QVERIFY(m_controller->deleteWebsite(id));

    QCOMPARE(m_controller->getWebsiteList(), QStringLiteral("[]"));
    QCOMPARE(m_controller->getHistoryList(), QStringLiteral("[]"));
    QCOMPARE(websiteSpy.count(), 1);
    QCOMPARE(historySpy.count(), 1);
}

void TestMainControllerWebsite::editWebsiteUrlChangeClosesSession()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    const QString id = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8())
                           .array().first().toObject().value(QStringLiteral("id")).toString();

    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));
    QVERIFY(QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8())
                .array().first().toObject().value(QStringLiteral("has_open")).toBool());

    const QString json = QStringLiteral("{\"id\":\"%1\",\"url\":\"https://other.example.com\"}").arg(id);
    QVERIFY(m_controller->editWebsite(json));

    QCOMPARE(m_controller->getSessionListJson(), QStringLiteral("[]"));
    QVERIFY(!QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8())
                 .array().first().toObject().value(QStringLiteral("has_open")).toBool());
}

void TestMainControllerWebsite::runImportsPresetsWhenDatabaseEmpty()
{
    m_controller->run();
    const QJsonDocument doc = QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8());
    QCOMPARE(doc.array().size(), builtInPresetWebsites().size());
}

void TestMainControllerWebsite::sessionListReflectsOpenSite()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));

    const QJsonArray sessions = QJsonDocument::fromJson(m_controller->getSessionListJson().toUtf8()).array();
    QCOMPARE(sessions.size(), 1);
    const QJsonObject session = sessions.first().toObject();
    QVERIFY(session.value(QStringLiteral("isActive")).toBool());
    QCOMPARE(session.value(QStringLiteral("tabCount")).toInt(), 1);
    QVERIFY(!session.value(QStringLiteral("isTemporary")).toBool());
}

void TestMainControllerWebsite::tabListReturnsTabsForSession()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));

    const QJsonArray sessions = QJsonDocument::fromJson(m_controller->getSessionListJson().toUtf8()).array();
    const QString sessionId = sessions.first().toObject().value(QStringLiteral("sessionId")).toString();

    const QJsonArray tabs = QJsonDocument::fromJson(m_controller->getTabListJson(sessionId).toUtf8()).array();
    QCOMPARE(tabs.size(), 1);
    QCOMPARE(tabs.first().toObject().value(QStringLiteral("url")).toString(),
             QStringLiteral("https://example.com"));
    QVERIFY(tabs.first().toObject().value(QStringLiteral("isActive")).toBool());
}

void TestMainControllerWebsite::moveSessionWrapsAround()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")).isEmpty());
    QVERIFY(m_controller->addWebsite(QStringLiteral("site-b"), QStringLiteral("https://b.example.com")).isEmpty());
    QCOMPARE(m_controller->openSite(QStringLiteral("https://a.example.com")),
             QStringLiteral("https://a.example.com"));
    QCOMPARE(m_controller->openSite(QStringLiteral("https://b.example.com")),
             QStringLiteral("https://b.example.com"));

    QCOMPARE(m_controller->sessionIndex(), 1);
    QVERIFY(m_controller->moveSession(1));
    QCOMPARE(m_controller->sessionIndex(), 0);
    QVERIFY(m_controller->moveSession(-1));
    QCOMPARE(m_controller->sessionIndex(), 1);
}

void TestMainControllerWebsite::moveTabWrapsAround()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));

    QCOMPARE(m_controller->tabIndex(), 0);
    QVERIFY(!m_controller->moveTab(1));
}

void TestMainControllerWebsite::confirmSelectionOpensSession()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));

    QVERIFY(m_controller->setFocusLayer(QStringLiteral("session")));
    const QString sessionUrl = m_controller->confirmSelection();
    QCOMPARE(sessionUrl, QStringLiteral("https://example.com"));
}

void TestMainControllerWebsite::activeTabCountReflectsSession()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site"), QStringLiteral("https://example.com")).isEmpty());
    QCOMPARE(m_controller->openSite(QStringLiteral("https://example.com")),
             QStringLiteral("https://example.com"));

    QCOMPARE(m_controller->activeTabCount(), 1);
    QCOMPARE(m_controller->activeTabIndex(), 0);
    QVERIFY(!m_controller->getSessionListJson().isEmpty());
}

void TestMainControllerWebsite::addWebsiteFetchesIconAsync()
{
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QByteArray pngBytes;
    {
        QBuffer buffer(&pngBytes);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "PNG"));
    }

    QTcpServer server;
    connect(&server, &QTcpServer::newConnection, &server, [&server, pngBytes]() {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket, pngBytes]() {
            socket->readAll();
            QByteArray response;
            response += "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: image/png\r\n";
            response += "Content-Length: ";
            response += QByteArray::number(pngBytes.size());
            response += "\r\nConnection: close\r\n\r\n";
            response += pngBytes;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    });
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const QString siteUrl = QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort());

    QSignalSpy spy(m_controller, &MainController::websiteListChange);
    QVERIFY(m_controller->addWebsite(QStringLiteral("local-site"), siteUrl).isEmpty());

    QString icon;
    for (int attempt = 0; attempt < 50; ++attempt) {
        const QJsonArray websites = QJsonDocument::fromJson(
            m_controller->getWebsiteList().toUtf8()).array();
        if (!websites.isEmpty()) {
            icon = websites.first().toObject().value(QStringLiteral("icon")).toString();
            if (icon.startsWith(QStringLiteral("file:"))) {
                break;
            }
        }
        QTest::qWait(100);
    }

    QVERIFY(icon.startsWith(QStringLiteral("file:")));
    QCOMPARE(QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8()).array().size(), 1);
    QVERIFY(spy.size() >= 2);
}

void TestMainControllerWebsite::temporarySessionFetchesIconAsync()
{
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(Qt::green);
    QByteArray pngBytes;
    {
        QBuffer buffer(&pngBytes);
        buffer.open(QIODevice::WriteOnly);
        QVERIFY(image.save(&buffer, "PNG"));
    }

    QTcpServer server;
    connect(&server, &QTcpServer::newConnection, &server, [&server, pngBytes]() {
        QTcpSocket *socket = server.nextPendingConnection();
        connect(socket, &QTcpSocket::readyRead, socket, [socket, pngBytes]() {
            socket->readAll();
            QByteArray response;
            response += "HTTP/1.1 200 OK\r\n";
            response += "Content-Type: image/png\r\n";
            response += "Content-Length: ";
            response += QByteArray::number(pngBytes.size());
            response += "\r\nConnection: close\r\n\r\n";
            response += pngBytes;
            socket->write(response);
            socket->flush();
            socket->disconnectFromHost();
        });
    });
    QVERIFY(server.listen(QHostAddress::LocalHost));

    const QString siteUrl = QStringLiteral("http://127.0.0.1:%1/").arg(server.serverPort());

    const QString opened = m_controller->openSite(siteUrl);
    QVERIFY(!opened.isEmpty());
    QCOMPARE(QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8()).array().size(), 0);

    QString icon;
    for (int attempt = 0; attempt < 50; ++attempt) {
        const QJsonArray sessions = QJsonDocument::fromJson(
            m_controller->getSessionListJson().toUtf8()).array();
        if (!sessions.isEmpty()) {
            const QJsonObject session = sessions.first().toObject();
            QVERIFY(session.value(QStringLiteral("isTemporary")).toBool());
            icon = session.value(QStringLiteral("icon")).toString();
            if (icon.startsWith(QStringLiteral("file:"))) {
                break;
            }
        }
        QTest::qWait(100);
    }

    QVERIFY(icon.startsWith(QStringLiteral("file:")));
    QCOMPARE(QJsonDocument::fromJson(m_controller->getWebsiteList().toUtf8()).array().size(), 0);
}

QTEST_CORE_MAIN(TestMainControllerWebsite)
#include "tst_maincontroller_website.moc"
