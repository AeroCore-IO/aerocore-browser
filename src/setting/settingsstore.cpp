#include "setting/settingsstore.h"

#include <QCoreApplication>
#include <QSettings>
#include <QStandardPaths>
#include <algorithm>

namespace {

QString normalizedGpuMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    return normalized.isEmpty() ? QStringLiteral("auto") : normalized;
}

QSettings makeSettings(const QString &storagePath)
{
    if (storagePath.isEmpty()) {
        return QSettings();
    }

    return QSettings(storagePath, QSettings::IniFormat);
}

} // namespace

SettingsStore &SettingsStore::instance()
{
    static SettingsStore store;
    return store;
}

void SettingsStore::setStoragePath(const QString &path)
{
    m_storagePath = path;
}

QString SettingsStore::storagePath() const
{
    return m_storagePath;
}

QString SettingsStore::gpuMode() const
{
    QSettings settings = makeSettings(m_storagePath);
    return normalizedGpuMode(settings.value(QStringLiteral("cef/gpuMode")).toString());
}

void SettingsStore::setGpuMode(const QString &mode)
{
    QSettings settings = makeSettings(m_storagePath);
    settings.setValue(QStringLiteral("cef/gpuMode"), normalizedGpuMode(mode));
}

double SettingsStore::cursorSpeedScale() const
{
    QSettings settings = makeSettings(m_storagePath);
    return std::clamp(settings.value(QStringLiteral("input/cursorSpeed"), 1.0).toDouble(), 0.5, 2.0);
}

void SettingsStore::setCursorSpeedScale(double scale)
{
    QSettings settings = makeSettings(m_storagePath);
    settings.setValue(QStringLiteral("input/cursorSpeed"), std::clamp(scale, 0.5, 2.0));
}

double SettingsStore::wheelSpeedScale() const
{
    QSettings settings = makeSettings(m_storagePath);
    return std::clamp(settings.value(QStringLiteral("input/wheelSpeed"), 1.0).toDouble(), 0.5, 2.0);
}

void SettingsStore::setWheelSpeedScale(double scale)
{
    QSettings settings = makeSettings(m_storagePath);
    settings.setValue(QStringLiteral("input/wheelSpeed"), std::clamp(scale, 0.5, 2.0));
}

int SettingsStore::cursorSize() const
{
    QSettings settings = makeSettings(m_storagePath);
    return std::clamp(settings.value(QStringLiteral("input/cursorSize"), 56).toInt(), 16, 96);
}

void SettingsStore::setCursorSize(int size)
{
    QSettings settings = makeSettings(m_storagePath);
    settings.setValue(QStringLiteral("input/cursorSize"), std::clamp(size, 16, 96));
}

int SettingsStore::cursorAutoHideDelay() const
{
    QSettings settings = makeSettings(m_storagePath);
    return std::clamp(settings.value(QStringLiteral("input/cursorAutoHideDelay"), 3).toInt(), 0, 10);
}

void SettingsStore::setCursorAutoHideDelay(int seconds)
{
    QSettings settings = makeSettings(m_storagePath);
    settings.setValue(QStringLiteral("input/cursorAutoHideDelay"), std::clamp(seconds, 0, 10));
}

bool SettingsStore::edgeScrollEnabled() const
{
    QSettings settings = makeSettings(m_storagePath);
    return settings.value(QStringLiteral("input/edgeScroll"), true).toBool();
}

void SettingsStore::setEdgeScrollEnabled(bool enabled)
{
    QSettings settings = makeSettings(m_storagePath);
    settings.setValue(QStringLiteral("input/edgeScroll"), enabled);
}

QVector<GamepadCatalog::DeviceRecord> SettingsStore::rememberedGamepads() const
{
    QSettings settings = makeSettings(m_storagePath);
    QVector<GamepadCatalog::DeviceRecord> records;
    const int count = settings.beginReadArray(QStringLiteral("input/gamepads"));
    records.reserve(count);
    for (int index = 0; index < count; ++index) {
        settings.setArrayIndex(index);
        GamepadCatalog::DeviceRecord record;
        record.instanceId = settings.value(QStringLiteral("instanceId")).toString();
        if (record.instanceId.isEmpty()) {
            continue;
        }
        record.name = settings.value(QStringLiteral("name")).toString();
        record.detectedKindId = settings.value(QStringLiteral("kind"), GamepadCatalog::kKindGeneric).toString();
        record.layoutOverride =
            GamepadCatalog::normalizeOverride(settings.value(QStringLiteral("layoutOverride")).toString());
        record.lastUsedMs = settings.value(QStringLiteral("lastUsedMs"), 0).toLongLong();
        record.builtin = settings.value(QStringLiteral("builtin"), false).toBool();
        records.push_back(record);
    }
    settings.endArray();
    return records;
}

void SettingsStore::setRememberedGamepads(const QVector<GamepadCatalog::DeviceRecord> &records)
{
    QSettings settings = makeSettings(m_storagePath);
    settings.remove(QStringLiteral("input/gamepads"));
    settings.beginWriteArray(QStringLiteral("input/gamepads"));
    int index = 0;
    for (const GamepadCatalog::DeviceRecord &record : records) {
        if (record.instanceId.isEmpty()) {
            continue;
        }
        settings.setArrayIndex(index++);
        settings.setValue(QStringLiteral("instanceId"), record.instanceId);
        settings.setValue(QStringLiteral("name"), record.name);
        settings.setValue(QStringLiteral("kind"), record.detectedKindId);
        settings.setValue(QStringLiteral("layoutOverride"), GamepadCatalog::normalizeOverride(record.layoutOverride));
        settings.setValue(QStringLiteral("lastUsedMs"), record.lastUsedMs);
        settings.setValue(QStringLiteral("builtin"), record.builtin);
    }
    settings.endArray();
}

QString SettingsStore::lastExclusiveGamepadId() const
{
    QSettings settings = makeSettings(m_storagePath);
    return settings.value(QStringLiteral("input/lastExclusiveGamepadId")).toString();
}

void SettingsStore::setLastExclusiveGamepadId(const QString &instanceId)
{
    QSettings settings = makeSettings(m_storagePath);
    if (instanceId.isEmpty()) {
        settings.remove(QStringLiteral("input/lastExclusiveGamepadId"));
        return;
    }
    settings.setValue(QStringLiteral("input/lastExclusiveGamepadId"), instanceId);
}

QString SettingsStore::lastButtonIconLayout() const
{
    QSettings settings = makeSettings(m_storagePath);
    return GamepadCatalog::normalizeLayoutId(
        settings.value(QStringLiteral("input/lastButtonIconLayout")).toString());
}

void SettingsStore::setLastButtonIconLayout(const QString &layoutId)
{
    QSettings settings = makeSettings(m_storagePath);
    settings.setValue(QStringLiteral("input/lastButtonIconLayout"),
                      GamepadCatalog::normalizeLayoutId(layoutId));
}

QString SettingsStore::resolveGpuMode()
{
    const QString envMode = qEnvironmentVariable("AEROCORE_GPU_MODE").trimmed();
    if (!envMode.isEmpty()) {
        return normalizedGpuMode(envMode);
    }

    return SettingsStore::instance().gpuMode();
}

void SettingsStore::loadInto(SiteConfigManager &siteConfig)
{
    QSettings settings = makeSettings(m_storagePath);

    SiteConfigSnapshot snapshot;
    snapshot.hasExplicitAppUiScale = settings.value(QStringLiteral("ui/appScaleExplicit"), false).toBool();
    if (snapshot.hasExplicitAppUiScale) {
        snapshot.appUiScale = std::clamp(settings.value(QStringLiteral("ui/appScale"), 1.0).toDouble(), 1.0, 2.4);
    }

    snapshot.hasExplicitDefaultZoomFactor =
        settings.value(QStringLiteral("readability/defaultZoomFactorExplicit"), false).toBool();
    if (snapshot.hasExplicitDefaultZoomFactor) {
        snapshot.defaultZoomFactor = settings.value(QStringLiteral("readability/defaultZoomFactor"), 1.2).toDouble();
    }

    snapshot.hasExplicitDefaultMinimumFontSize =
        settings.value(QStringLiteral("readability/defaultMinimumFontSizeExplicit"), false).toBool();
    if (snapshot.hasExplicitDefaultMinimumFontSize) {
        snapshot.defaultMinimumFontSize =
            settings.value(QStringLiteral("readability/defaultMinimumFontSize"), 20).toInt();
    }

    snapshot.siteConfigs.clear();
    const int siteCount = settings.beginReadArray(QStringLiteral("siteConfigs"));
    for (int index = 0; index < siteCount; ++index) {
        settings.setArrayIndex(index);
        const QString host = settings.value(QStringLiteral("host")).toString().trimmed().toLower();
        if (host.isEmpty()) {
            continue;
        }

        SiteConfig config;
        config.zoomFactor = settings.value(QStringLiteral("zoomFactor"), 1.2).toDouble();
        config.minimumFontSize = settings.value(QStringLiteral("minimumFontSize"), 20).toInt();
        config.desktopMode = settings.value(QStringLiteral("desktopMode"), true).toBool();
        config.readabilityMode = settings.value(QStringLiteral("readabilityMode"), QStringLiteral("sofa")).toString();
        snapshot.siteConfigs.insert(host, config);
    }
    settings.endArray();

    siteConfig.applySnapshot(snapshot);
}

void SettingsStore::saveFrom(const SiteConfigManager &siteConfig)
{
    QSettings settings = makeSettings(m_storagePath);
    const SiteConfigSnapshot snapshot = siteConfig.exportSnapshot();

    if (snapshot.hasExplicitAppUiScale) {
        settings.setValue(QStringLiteral("ui/appScale"), snapshot.appUiScale);
        settings.setValue(QStringLiteral("ui/appScaleExplicit"), true);
    } else {
        settings.remove(QStringLiteral("ui/appScale"));
        settings.remove(QStringLiteral("ui/appScaleExplicit"));
    }

    if (snapshot.hasExplicitDefaultZoomFactor) {
        settings.setValue(QStringLiteral("readability/defaultZoomFactor"), snapshot.defaultZoomFactor);
        settings.setValue(QStringLiteral("readability/defaultZoomFactorExplicit"), true);
    } else {
        settings.remove(QStringLiteral("readability/defaultZoomFactor"));
        settings.remove(QStringLiteral("readability/defaultZoomFactorExplicit"));
    }

    if (snapshot.hasExplicitDefaultMinimumFontSize) {
        settings.setValue(QStringLiteral("readability/defaultMinimumFontSize"), snapshot.defaultMinimumFontSize);
        settings.setValue(QStringLiteral("readability/defaultMinimumFontSizeExplicit"), true);
    } else {
        settings.remove(QStringLiteral("readability/defaultMinimumFontSize"));
        settings.remove(QStringLiteral("readability/defaultMinimumFontSizeExplicit"));
    }

    settings.beginWriteArray(QStringLiteral("siteConfigs"));
    int index = 0;
    for (auto it = snapshot.siteConfigs.constBegin(); it != snapshot.siteConfigs.constEnd(); ++it) {
        settings.setArrayIndex(index++);
        settings.setValue(QStringLiteral("host"), it.key());
        settings.setValue(QStringLiteral("zoomFactor"), it.value().zoomFactor);
        settings.setValue(QStringLiteral("minimumFontSize"), it.value().minimumFontSize);
        settings.setValue(QStringLiteral("desktopMode"), it.value().desktopMode);
        settings.setValue(QStringLiteral("readabilityMode"), it.value().readabilityMode);
    }
    settings.endArray();
}
