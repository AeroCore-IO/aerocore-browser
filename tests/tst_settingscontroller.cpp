#include "setting/settingscontroller.h"
#include "setting/settingsstore.h"
#include "siteconfigmanager.h"

#include "browsercontroller.h"
#include "inputmanager.h"
#include "maincontroller.h"
#include "database/databasemanager.h"
#include "history/historyrepository.h"
#include "website/websiterepository.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include "test_util.h"
#include "test_util_gui.h"

class TestSettingsController : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void setAppUiScalePersistsAndEmits();
    void resetAppUiScaleClearsExplicit();
    void setDefaultZoomFactorPersists();
    void resetDefaultZoomClearsExplicit();
    void resetDefaultMinimumFontSizeClearsExplicit();
    void setZoomForUrlHostRoundtrip();
    void setReadabilityModeForUrl();
    void resetSiteRemovesHostOverride();
    void preciseModeReadOnlyProxy();
    void applyToCurrentUrlWithoutBrowser();
    void setGpuModeSetsRestartRequired();
    void gpuModeChangePersists();
    void clearBrowsingHistoryDelegates();
    void cursorSpeedScaleAppliesAndPersists();
    void wheelSpeedScaleClampedAndPersists();
    void cursorSizePersistsAndReloads();
    void cursorAutoHideDelayPersistsAndReloads();
    void edgeScrollToggleAppliesAndPersists();

private:
    void useIsolatedSettings();

    QTemporaryDir m_tempDir;
    SiteConfigManager *m_siteConfig = nullptr;
    MainController *m_mainController = nullptr;
    BrowserController *m_browserController = nullptr;
};

void TestSettingsController::init()
{
    QVERIFY(m_tempDir.isValid());
    useIsolatedSettings();
    QFile::remove(m_tempDir.filePath(QStringLiteral("settings.ini")));

    m_siteConfig = new SiteConfigManager();
    m_mainController = new MainController();
    QVERIFY(m_mainController->initializeWebsiteDatabase(m_tempDir.path() + QStringLiteral("/test.db")));
    m_browserController = new BrowserController();
}

void TestSettingsController::cleanup()
{
    delete m_browserController;
    m_browserController = nullptr;
    delete m_mainController;
    m_mainController = nullptr;
    delete m_siteConfig;
    m_siteConfig = nullptr;
    DatabaseManager::close();
}

void TestSettingsController::useIsolatedSettings()
{
    QCoreApplication::setOrganizationName(QStringLiteral("AeroCoreTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SettingsControllerTest"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    SettingsStore::instance().setStoragePath(m_tempDir.filePath(QStringLiteral("settings.ini")));
}

void TestSettingsController::setAppUiScalePersistsAndEmits()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    QSignalSpy spy(&controller, &SettingsController::appUiScaleChanged);

    controller.setAppUiScale(1.5);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(controller.appUiScale(), 1.5);

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);
    QCOMPARE(reloaded.appUiScale(), 1.5);
}

void TestSettingsController::resetAppUiScaleClearsExplicit()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    controller.setAppUiScale(1.5);
    controller.resetAppUiScale();

    QVERIFY(!controller.appUiScaleExplicit());
}

void TestSettingsController::setDefaultZoomFactorPersists()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    QSignalSpy spy(&controller, &SettingsController::defaultsChanged);

    controller.setDefaultZoomFactor(1.7);
    QCOMPARE(spy.count(), 1);

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);
    QCOMPARE(reloaded.defaultZoomFactor(), 1.7);
}

void TestSettingsController::resetDefaultZoomClearsExplicit()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    controller.setDefaultZoomFactor(1.7);
    controller.resetDefaultZoom();

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);
    QVERIFY(!reloaded.hasExplicitDefaultZoomFactor());
}

void TestSettingsController::resetDefaultMinimumFontSizeClearsExplicit()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    controller.setDefaultMinimumFontSize(24);
    controller.resetDefaultMinimumFontSize();

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);
    QVERIFY(!reloaded.hasExplicitDefaultMinimumFontSize());
}

void TestSettingsController::setZoomForUrlHostRoundtrip()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    controller.setZoomForUrl(QStringLiteral("https://host-b.test/page"), 1.9);

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);
    QCOMPARE(reloaded.zoomForUrl(QStringLiteral("https://host-b.test")), 1.9);
}

void TestSettingsController::setReadabilityModeForUrl()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    QSignalSpy spy(&controller, &SettingsController::siteConfigChanged);

    controller.setReadabilityModeForUrl(QStringLiteral("https://host-c.test"), QStringLiteral("large"));
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.at(0).at(0).toString(), QStringLiteral("host-c.test"));
}

void TestSettingsController::resetSiteRemovesHostOverride()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    controller.setZoomForUrl(QStringLiteral("https://host-d.test"), 2.0);
    controller.resetSite(QStringLiteral("https://host-d.test"));

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);
    QCOMPARE(reloaded.zoomForUrl(QStringLiteral("https://host-d.test")), reloaded.defaultZoomFactor());
}

void TestSettingsController::preciseModeReadOnlyProxy()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    m_mainController->getInputManager()->setPreciseMode(true);
    QVERIFY(controller.preciseMode());
}

void TestSettingsController::applyToCurrentUrlWithoutBrowser()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    controller.applyToCurrentUrl();
}

void TestSettingsController::setGpuModeSetsRestartRequired()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    QSignalSpy spy(&controller, &SettingsController::restartRequiredChanged);

    controller.setGpuMode(QStringLiteral("unsafe"));
    QCOMPARE(spy.count(), 1);
    QVERIFY(controller.restartRequired());
}

void TestSettingsController::gpuModeChangePersists()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    controller.setGpuMode(QStringLiteral("unsafe"));
    QCOMPARE(SettingsStore::instance().gpuMode(), QStringLiteral("unsafe"));
}

void TestSettingsController::clearBrowsingHistoryDelegates()
{
    WebsiteRepository websiteRepository;
    QVERIFY(websiteRepository.add(QStringLiteral("Example"), QStringLiteral("https://example.com")));

    HistoryRepository historyRepository;
    QVERIFY(historyRepository.upsertVisit(QStringLiteral("Example"), QStringLiteral("https://example.com")));

    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    controller.clearBrowsingHistory();
    QCOMPARE(m_mainController->getHistoryList(), QStringLiteral("[]"));
}

void TestSettingsController::cursorSpeedScaleAppliesAndPersists()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    QSignalSpy spy(&controller, &SettingsController::cursorSpeedScaleChanged);

    controller.setCursorSpeedScale(1.5);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(controller.cursorSpeedScale(), 1.5);
    QCOMPARE(m_mainController->getInputManager()->cursorSpeedScale(), 1.5);
    QCOMPARE(SettingsStore::instance().cursorSpeedScale(), 1.5);
}

void TestSettingsController::wheelSpeedScaleClampedAndPersists()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);

    controller.setWheelSpeedScale(5.0);
    QCOMPARE(controller.wheelSpeedScale(), 2.0);
    QCOMPARE(SettingsStore::instance().wheelSpeedScale(), 2.0);

    controller.setWheelSpeedScale(0.1);
    QCOMPARE(controller.wheelSpeedScale(), 0.5);
    QCOMPARE(SettingsStore::instance().wheelSpeedScale(), 0.5);
}

void TestSettingsController::cursorSizePersistsAndReloads()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    QCOMPARE(controller.cursorSize(), 56);

    QSignalSpy spy(&controller, &SettingsController::cursorSizeChanged);
    controller.setCursorSize(68);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(controller.cursorSize(), 68);
    QCOMPARE(SettingsStore::instance().cursorSize(), 68);

    SettingsController reloaded(m_siteConfig,
                                m_mainController->getInputManager(),
                                m_mainController,
                                m_browserController);
    QCOMPARE(reloaded.cursorSize(), 68);
}

void TestSettingsController::cursorAutoHideDelayPersistsAndReloads()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    QCOMPARE(controller.cursorAutoHideDelay(), 3);

    QSignalSpy spy(&controller, &SettingsController::cursorAutoHideDelayChanged);
    controller.setCursorAutoHideDelay(7);
    QCOMPARE(spy.count(), 1);
    QCOMPARE(controller.cursorAutoHideDelay(), 7);
    QCOMPARE(SettingsStore::instance().cursorAutoHideDelay(), 7);

    controller.setCursorAutoHideDelay(0);
    QCOMPARE(controller.cursorAutoHideDelay(), 0);

    controller.setCursorAutoHideDelay(99);
    QCOMPARE(controller.cursorAutoHideDelay(), 10);

    SettingsController reloaded(m_siteConfig,
                                m_mainController->getInputManager(),
                                m_mainController,
                                m_browserController);
    QCOMPARE(reloaded.cursorAutoHideDelay(), 10);
}

void TestSettingsController::edgeScrollToggleAppliesAndPersists()
{
    SettingsController controller(m_siteConfig,
                                  m_mainController->getInputManager(),
                                  m_mainController,
                                  m_browserController);
    QVERIFY(controller.edgeScrollEnabled());

    QSignalSpy spy(&controller, &SettingsController::edgeScrollEnabledChanged);
    controller.setEdgeScrollEnabled(false);
    QCOMPARE(spy.count(), 1);
    QVERIFY(!controller.edgeScrollEnabled());
    QVERIFY(!m_mainController->getInputManager()->edgeScrollEnabled());
    QVERIFY(!SettingsStore::instance().edgeScrollEnabled());

    SettingsController reloaded(m_siteConfig,
                                m_mainController->getInputManager(),
                                m_mainController,
                                m_browserController);
    QVERIFY(!reloaded.edgeScrollEnabled());
}

#include "tst_settingscontroller.moc"

QTEST_GUI_MAIN(TestSettingsController)
