#include "setting/cefpaths.h"
#include "setting/settingsstore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>

#include "test_util.h"

class TestCefSettingsService : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cefProfilePathUsesAppDataLocation();
    void cefLogPathEndsWithCefLog();
    void appVersionMatchesApplication();
    void resolveGpuModeEnvOverridesStore();
    void resolveGpuModeUsesStoreWhenEnvEmpty();

private:
    void useIsolatedSettings();

    QTemporaryDir m_tempDir;
};

void TestCefSettingsService::init()
{
    QVERIFY(m_tempDir.isValid());
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));
    useIsolatedSettings();
}

void TestCefSettingsService::useIsolatedSettings()
{
    QCoreApplication::setOrganizationName(QStringLiteral("AeroCoreTest"));
    QCoreApplication::setApplicationName(QStringLiteral("CefSettingsServiceTest"));
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_tempDir.path());
    SettingsStore::instance().setStoragePath(m_tempDir.filePath(QStringLiteral("settings.ini")));
}

void TestCefSettingsService::cefProfilePathUsesAppDataLocation()
{
    QVERIFY(CefPaths::profilePath().contains(QStringLiteral("cef-profile")));
}

void TestCefSettingsService::cefLogPathEndsWithCefLog()
{
    QVERIFY(CefPaths::logPath().endsWith(QStringLiteral("cef.log")));
}

void TestCefSettingsService::appVersionMatchesApplication()
{
    QCOMPARE(QCoreApplication::applicationVersion(), QStringLiteral("0.1.0"));
}

void TestCefSettingsService::resolveGpuModeEnvOverridesStore()
{
    qputenv("AEROCORE_GPU_MODE", "unsafe");
    SettingsStore::instance().setGpuMode(QStringLiteral("auto"));
    QCOMPARE(SettingsStore::resolveGpuMode(), QStringLiteral("unsafe"));
    qunsetenv("AEROCORE_GPU_MODE");
}

void TestCefSettingsService::resolveGpuModeUsesStoreWhenEnvEmpty()
{
    qunsetenv("AEROCORE_GPU_MODE");
    SettingsStore::instance().setGpuMode(QStringLiteral("x11-egl"));
    QCOMPARE(SettingsStore::resolveGpuMode(), QStringLiteral("x11-egl"));
}

QTEST_CORE_MAIN(TestCefSettingsService)
#include "tst_cefsettingsservice.moc"
