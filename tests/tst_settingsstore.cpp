#include "setting/settingsstore.h"
#include "siteconfigmanager.h"
#include "gamepadcatalog.h"

#include <QCoreApplication>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "test_util.h"

class TestSettingsStore : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void loadIntoEmptyStoreUsesDefaults();
    void saveAndLoadRoundtripGlobalKeys();
    void saveAndLoadSiteConfigsArray();
    void removeSiteConfigPersists();
    void gpuModeDefaultsToAuto();
    void gpuModeRoundtrip();
    void cursorAutoHideDelayDefaultsToThree();
    void cursorAutoHideDelayClampedAndPersists();
    void doesNotTouchSessionKeys();
    void loadIntoToleratesMissingKeys();
    void refreshAutomaticReadabilityPreservesExplicit();
    void pageOpenResolvesZoomAndMinFontFromHostConfig();
    void perSiteMinimumFontSurvivesReload();
    void scaledFourKMatchesNativeFourKPageZoom();
    void gamescopeScaledFourKIsNotHandheld();
    void rememberedGamepadsRoundtrip();
    void lastButtonIconLayoutNormalizesInvalid();

private:
    void useIsolatedSettings();

    QTemporaryDir m_tempDir;
};

void TestSettingsStore::init()
{
    QVERIFY(m_tempDir.isValid());
    useIsolatedSettings();
    QFile::remove(m_tempDir.filePath(QStringLiteral("settings.ini")));
}

void TestSettingsStore::useIsolatedSettings()
{
    QCoreApplication::setOrganizationName(QStringLiteral("AeroCoreTest"));
    QCoreApplication::setApplicationName(QStringLiteral("SettingsStoreTest"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    SettingsStore::instance().setStoragePath(m_tempDir.filePath(QStringLiteral("settings.ini")));
}

void TestSettingsStore::loadIntoEmptyStoreUsesDefaults()
{
    SiteConfigManager siteConfig;
    SettingsStore::instance().loadInto(siteConfig);

    QVERIFY(!siteConfig.hasExplicitAppUiScale());
    QVERIFY(!siteConfig.hasExplicitDefaultZoomFactor());
    QVERIFY(!siteConfig.hasExplicitDefaultMinimumFontSize());
    QCOMPARE(siteConfig.zoomForUrl(QStringLiteral("https://example.com")), siteConfig.defaultZoomFactor());
}

void TestSettingsStore::saveAndLoadRoundtripGlobalKeys()
{
    SiteConfigManager siteConfig;
    siteConfig.setAppUiScale(1.5);
    siteConfig.setDefaultZoomFactor(1.8);
    siteConfig.setDefaultMinimumFontSize(24);

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);

    QVERIFY(reloaded.hasExplicitAppUiScale());
    QCOMPARE(reloaded.appUiScale(), 1.5);
    QVERIFY(reloaded.hasExplicitDefaultZoomFactor());
    QCOMPARE(reloaded.defaultZoomFactor(), 1.8);
    QVERIFY(reloaded.hasExplicitDefaultMinimumFontSize());
    QCOMPARE(reloaded.defaultMinimumFontSize(), 24);
}

void TestSettingsStore::saveAndLoadSiteConfigsArray()
{
    SiteConfigManager siteConfig;
    siteConfig.setZoomForUrl(QStringLiteral("https://www.example.com"), 1.6);
    siteConfig.setReadabilityModeForUrl(QStringLiteral("https://other.example.com"), QStringLiteral("large"));

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);

    QCOMPARE(reloaded.zoomForUrl(QStringLiteral("https://www.example.com/page")), 1.6);
    QCOMPARE(reloaded.readabilityModeForUrl(QStringLiteral("https://other.example.com")), QStringLiteral("large"));
}

void TestSettingsStore::removeSiteConfigPersists()
{
    SiteConfigManager siteConfig;
    siteConfig.setZoomForUrl(QStringLiteral("https://host-a.test"), 1.7);
    siteConfig.resetSite(QStringLiteral("https://host-a.test"));

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);

    QCOMPARE(reloaded.zoomForUrl(QStringLiteral("https://host-a.test")), reloaded.defaultZoomFactor());
}

void TestSettingsStore::gpuModeDefaultsToAuto()
{
    QCOMPARE(SettingsStore::instance().gpuMode(), QStringLiteral("auto"));
}

void TestSettingsStore::gpuModeRoundtrip()
{
    const QString settingsPath = SettingsStore::instance().storagePath();
    SettingsStore::instance().setGpuMode(QStringLiteral("unsafe"));

    QSettings settings(settingsPath, QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("cef/gpuMode")).toString(), QStringLiteral("unsafe"));
}

void TestSettingsStore::cursorAutoHideDelayDefaultsToThree()
{
    QCOMPARE(SettingsStore::instance().cursorAutoHideDelay(), 3);
}

void TestSettingsStore::cursorAutoHideDelayClampedAndPersists()
{
    SettingsStore::instance().setCursorAutoHideDelay(7);
    QCOMPARE(SettingsStore::instance().cursorAutoHideDelay(), 7);

    SettingsStore::instance().setCursorAutoHideDelay(99);
    QCOMPARE(SettingsStore::instance().cursorAutoHideDelay(), 10);
    SettingsStore::instance().setCursorAutoHideDelay(-3);
    QCOMPARE(SettingsStore::instance().cursorAutoHideDelay(), 0);

    SettingsStore::instance().setCursorAutoHideDelay(5);
    const QString settingsPath = SettingsStore::instance().storagePath();
    QSettings settings(settingsPath, QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("input/cursorAutoHideDelay")).toInt(), 5);
}

void TestSettingsStore::doesNotTouchSessionKeys()
{
    {
        QSettings settings(SettingsStore::instance().storagePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("sessions/activeSessionId"), QStringLiteral("session-1"));
    }

    SiteConfigManager siteConfig;
    siteConfig.setAppUiScale(1.3);

    QSettings settings(SettingsStore::instance().storagePath(), QSettings::IniFormat);
    QCOMPARE(settings.value(QStringLiteral("sessions/activeSessionId")).toString(), QStringLiteral("session-1"));
}

void TestSettingsStore::loadIntoToleratesMissingKeys()
{
    {
        QSettings settings(SettingsStore::instance().storagePath(), QSettings::IniFormat);
        settings.setValue(QStringLiteral("ui/appScaleExplicit"), true);
        settings.setValue(QStringLiteral("ui/appScale"), QStringLiteral("not-a-number"));
    }

    SiteConfigManager siteConfig;
    SettingsStore::instance().loadInto(siteConfig);

    QVERIFY(siteConfig.hasExplicitAppUiScale());
    QCOMPARE(siteConfig.appUiScale(), 1.0);
}

void TestSettingsStore::refreshAutomaticReadabilityPreservesExplicit()
{
    SiteConfigManager siteConfig;
    SettingsStore::instance().loadInto(siteConfig);
    siteConfig.setAppUiScale(1.6);
    const double explicitScale = siteConfig.appUiScale();

    siteConfig.refreshAutomaticReadability();

    QVERIFY(siteConfig.hasExplicitAppUiScale());
    QCOMPARE(siteConfig.appUiScale(), explicitScale);
}

// Mirrors BrowserContainer::applyReadability(url) resolution when opening page scope:
// zoomForUrl + minimumFontForUrl via configForHost.
void TestSettingsStore::pageOpenResolvesZoomAndMinFontFromHostConfig()
{
    SiteConfigManager siteConfig;
    SettingsStore::instance().loadInto(siteConfig);

    const QString url = QStringLiteral("https://fresh.example/");
    QCOMPARE(siteConfig.zoomForUrl(url), siteConfig.defaultZoomFactor());
    QCOMPARE(siteConfig.minimumFontForUrl(url), siteConfig.defaultMinimumFontSize());

    siteConfig.setDefaultZoomFactor(1.5);
    siteConfig.setDefaultMinimumFontSize(22);
    QCOMPARE(siteConfig.zoomForUrl(url), 1.5);
    QCOMPARE(siteConfig.minimumFontForUrl(url), 22);

    siteConfig.setZoomForUrl(url, 1.9);
    QCOMPARE(siteConfig.zoomForUrl(url), 1.9);
    // Per-site zoom must not change min font resolution for a fresh host write.
    QCOMPARE(siteConfig.minimumFontForUrl(url), 22);
}

void TestSettingsStore::perSiteMinimumFontSurvivesReload()
{
    SiteConfigManager siteConfig;
    SettingsStore::instance().loadInto(siteConfig);
    siteConfig.setDefaultMinimumFontSize(20);

    const QString url = QStringLiteral("https://font-host.test/");
    siteConfig.setMinimumFontSizeForUrl(url, 26);
    QCOMPARE(siteConfig.minimumFontForUrl(url), 26);

    SiteConfigManager reloaded;
    SettingsStore::instance().loadInto(reloaded);
    // applyReadability on page open uses minimumFontForUrl — must keep per-site value.
    QCOMPARE(reloaded.minimumFontForUrl(url), 26);
}

void TestSettingsStore::scaledFourKMatchesNativeFourKPageZoom()
{
    constexpr double kAnchorDiagonal = 45.88;
    const double nativeZoom =
        SiteConfigManager::probeAutomaticPageZoom(3840, 2160, 1.0, kAnchorDiagonal);
    const double scaledZoom =
        SiteConfigManager::probeAutomaticPageZoom(1920, 1080, 2.0, kAnchorDiagonal);

    QCOMPARE(SiteConfigManager::probeAutomaticTierLabel(3840, 2160, 1.0, kAnchorDiagonal),
             QStringLiteral("TV-4K"));
    QCOMPARE(SiteConfigManager::probeAutomaticTierLabel(1920, 1080, 2.0, kAnchorDiagonal),
             QStringLiteral("TV-4K"));
    QCOMPARE(scaledZoom, nativeZoom);
    QVERIFY(nativeZoom >= 1.85);
}

void TestSettingsStore::gamescopeScaledFourKIsNotHandheld()
{
    const QByteArray previousDesktop = qgetenv("XDG_CURRENT_DESKTOP");
    qputenv("XDG_CURRENT_DESKTOP", "gamescope:bazzite");

    QCOMPARE(SiteConfigManager::probeAutomaticTierLabel(1920, 1080, 2.0, 45.88),
             QStringLiteral("TV-4K"));
    QVERIFY(SiteConfigManager::probeAutomaticPageZoom(1920, 1080, 2.0, 45.88) >= 1.85);

    // Same machine as production logs: UHD + gamescope but monitor EDID ~27".
    // Must use couch anchor (~1.90), not small-screen floor (1.63).
    QCOMPARE(SiteConfigManager::probeAutomaticTierLabel(3840, 2160, 1.0, 27.15),
             QStringLiteral("TV-4K"));
    const double zoomWithBadEdid =
        SiteConfigManager::probeAutomaticPageZoom(3840, 2160, 1.0, 27.15);
    QVERIFY2(qAbs(zoomWithBadEdid - 1.90) < 0.02,
             qPrintable(QStringLiteral("expected ~1.90 got %1").arg(zoomWithBadEdid)));

    if (previousDesktop.isNull()) {
        qunsetenv("XDG_CURRENT_DESKTOP");
    } else {
        qputenv("XDG_CURRENT_DESKTOP", previousDesktop);
    }
}

void TestSettingsStore::rememberedGamepadsRoundtrip()
{
    QVector<GamepadCatalog::DeviceRecord> records;
    GamepadCatalog::DeviceRecord dual;
    dual.instanceId = QStringLiteral("aabbcc");
    dual.name = QStringLiteral("DualSense Wireless Controller");
    dual.detectedKindId = QStringLiteral("playstation");
    dual.layoutOverride = QStringLiteral("xbox");
    dual.lastUsedMs = 123;
    records.push_back(dual);

    SettingsStore::instance().setRememberedGamepads(records);
    SettingsStore::instance().setLastExclusiveGamepadId(QStringLiteral("aabbcc"));
    SettingsStore::instance().setLastButtonIconLayout(QStringLiteral("playstation"));

    const QVector<GamepadCatalog::DeviceRecord> loaded = SettingsStore::instance().rememberedGamepads();
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.first().instanceId, QStringLiteral("aabbcc"));
    QCOMPARE(loaded.first().name, QStringLiteral("DualSense Wireless Controller"));
    QCOMPARE(loaded.first().detectedKindId, QStringLiteral("playstation"));
    QCOMPARE(loaded.first().layoutOverride, QStringLiteral("xbox"));
    QCOMPARE(loaded.first().lastUsedMs, 123);
    QCOMPARE(SettingsStore::instance().lastExclusiveGamepadId(), QStringLiteral("aabbcc"));
    QCOMPARE(SettingsStore::instance().lastButtonIconLayout(), QStringLiteral("playstation"));
}

void TestSettingsStore::lastButtonIconLayoutNormalizesInvalid()
{
    SettingsStore::instance().setLastButtonIconLayout(QStringLiteral("auto"));
    QCOMPARE(SettingsStore::instance().lastButtonIconLayout(), QStringLiteral("xbox"));
    SettingsStore::instance().setLastButtonIconLayout(QStringLiteral("not-a-layout"));
    QCOMPARE(SettingsStore::instance().lastButtonIconLayout(), QStringLiteral("xbox"));
}

QTEST_CORE_MAIN(TestSettingsStore)
#include "tst_settingsstore.moc"
