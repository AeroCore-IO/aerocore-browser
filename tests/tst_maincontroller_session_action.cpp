#include "maincontroller.h"
#include "browsercontroller.h"
#include "database/databasemanager.h"
#include "website/websiteiconstorage.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTemporaryDir>
#include <QTest>

#include "test_util.h"

//
//
class TestMainControllerSessionAction : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void showBrowserAndGoHomeToggleHomeVisible();
    void nextSessionOnHomeOnlyMovesSelection();
    void nextSessionInBrowserActivatesAdjacentSession();
    void restoresPersistedSessionsAsPlaceholders();

private:
    void openTwoSessions();
    int activeSessionIndexInList() const;

    QTemporaryDir m_tempDir;
    MainController *m_controller = nullptr;
};

void TestMainControllerSessionAction::init()
{
    QVERIFY(m_tempDir.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("AeroCoreTest"));
    QCoreApplication::setApplicationName(QStringLiteral("MainControllerSessionActionTest"));
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

void TestMainControllerSessionAction::cleanup()
{
    delete m_controller;
    m_controller = nullptr;
    WebsiteIconStorage::setPresetIconSourceDirectory({});
    WebsiteIconStorage::setIconsStorageDirectory({});
    DatabaseManager::close();
}

void TestMainControllerSessionAction::openTwoSessions()
{
    QVERIFY(m_controller->addWebsite(QStringLiteral("site-a"), QStringLiteral("https://a.example.com")).isEmpty());
    QVERIFY(m_controller->addWebsite(QStringLiteral("site-b"), QStringLiteral("https://b.example.com")).isEmpty());
    QCOMPARE(m_controller->openSite(QStringLiteral("https://a.example.com")),
             QStringLiteral("https://a.example.com"));
    QCOMPARE(m_controller->openSite(QStringLiteral("https://b.example.com")),
             QStringLiteral("https://b.example.com"));
}

int TestMainControllerSessionAction::activeSessionIndexInList() const
{
    const QJsonArray sessions = QJsonDocument::fromJson(m_controller->getSessionListJson().toUtf8()).array();
    for (int index = 0; index < sessions.size(); ++index) {
        if (sessions.at(index).toObject().value(QStringLiteral("isActive")).toBool()) {
            return index;
        }
    }
    return -1;
}

void TestMainControllerSessionAction::showBrowserAndGoHomeToggleHomeVisible()
{
    BrowserController controller;
    QVERIFY(controller.homeVisible());

    QSignalSpy spy(&controller, &BrowserController::homeVisibleChanged);
    controller.showBrowser();
    QVERIFY(!controller.homeVisible());
    QCOMPARE(spy.count(), 1);

    controller.goHome();
    QVERIFY(controller.homeVisible());
    QCOMPARE(spy.count(), 2);

    controller.goHome();
    QVERIFY(controller.homeVisible());
    QCOMPARE(spy.count(), 2);
}

void TestMainControllerSessionAction::nextSessionOnHomeOnlyMovesSelection()
{
    openTwoSessions();
    QCOMPARE(activeSessionIndexInList(), 1);
    QCOMPARE(m_controller->sessionIndex(), 1);

    m_controller->handleBrowserAction(QStringLiteral("nextSession"));

    QCOMPARE(m_controller->sessionIndex(), 0);
    QCOMPARE(activeSessionIndexInList(), 1);
    QCOMPARE(m_controller->focusLayer(), QStringLiteral("session"));
}

void TestMainControllerSessionAction::nextSessionInBrowserActivatesAdjacentSession()
{
    openTwoSessions();
    QCOMPARE(activeSessionIndexInList(), 1);

    auto *browserController = new BrowserController(m_controller);
    browserController->showBrowser();
    m_controller->notifyBrowserControllerAvailable(browserController);
    QObject containerStub;
    m_controller->bindBrowserContainer(&containerStub);

    m_controller->handleBrowserAction(QStringLiteral("nextSession"));

    QCOMPARE(activeSessionIndexInList(), 0);
}

void TestMainControllerSessionAction::restoresPersistedSessionsAsPlaceholders()
{
    openTwoSessions();
    QCOMPARE(m_controller->openHistoryUrl(QStringLiteral("https://b.example.com/video")),
             QStringLiteral("https://b.example.com/video"));
    m_controller->persistSessionsNow();

    MainController restored;
    restored.initializeSessions();

    const QJsonArray sessions =
        QJsonDocument::fromJson(restored.getSessionListJson().toUtf8()).array();
    QCOMPARE(sessions.size(), 2);

    QString sessionAId;
    QString sessionBId;
    bool sessionBIsActive = false;
    for (const QJsonValue &value : sessions) {
        const QJsonObject obj = value.toObject();
        if (obj.value(QStringLiteral("sessionId")).toString() == QStringLiteral("https://a.example.com")) {
            sessionAId = obj.value(QStringLiteral("sessionId")).toString();
            QCOMPARE(obj.value(QStringLiteral("tabCount")).toInt(), 1);
        } else {
            sessionBId = obj.value(QStringLiteral("sessionId")).toString();
            QCOMPARE(obj.value(QStringLiteral("tabCount")).toInt(), 2);
            sessionBIsActive = obj.value(QStringLiteral("isActive")).toBool();
        }
    }
    QVERIFY(!sessionAId.isEmpty());
    QVERIFY(!sessionBId.isEmpty());

    QVERIFY(sessionBIsActive);

    const QJsonArray tabs =
        QJsonDocument::fromJson(restored.getTabListJson(sessionBId).toUtf8()).array();
    QCOMPARE(tabs.size(), 2);
    QString activeTabId;
    for (const QJsonValue &value : tabs) {
        const QJsonObject tab = value.toObject();
        if (tab.value(QStringLiteral("isActive")).toBool()) {
            activeTabId = tab.value(QStringLiteral("tabId")).toString();
            QCOMPARE(tab.value(QStringLiteral("url")).toString(),
                     QStringLiteral("https://b.example.com/video"));
        }
    }
    QVERIFY(!activeTabId.isEmpty());

    QCOMPARE(restored.openTab(activeTabId), QStringLiteral("https://b.example.com/video"));
    QCOMPARE(restored.openSession(sessionAId), QStringLiteral("https://a.example.com"));
}

QTEST_CORE_MAIN(TestMainControllerSessionAction)
#include "tst_maincontroller_session_action.moc"
