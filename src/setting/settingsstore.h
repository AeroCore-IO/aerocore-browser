#pragma once

#include "gamepadcatalog.h"
#include "siteconfigmanager.h"

#include <QString>
#include <QVector>

class SettingsStore
{
public:
    static SettingsStore &instance();

    void setStoragePath(const QString &path);
    QString storagePath() const;

    void loadInto(SiteConfigManager &siteConfig);
    void saveFrom(const SiteConfigManager &siteConfig);

    QString gpuMode() const;
    void setGpuMode(const QString &mode);

    double cursorSpeedScale() const;
    void setCursorSpeedScale(double scale);

    double wheelSpeedScale() const;
    void setWheelSpeedScale(double scale);

    int cursorSize() const;
    void setCursorSize(int size);

    int cursorAutoHideDelay() const;
    void setCursorAutoHideDelay(int seconds);

    bool edgeScrollEnabled() const;
    void setEdgeScrollEnabled(bool enabled);

    QVector<GamepadCatalog::DeviceRecord> rememberedGamepads() const;
    void setRememberedGamepads(const QVector<GamepadCatalog::DeviceRecord> &records);
    QString lastExclusiveGamepadId() const;
    void setLastExclusiveGamepadId(const QString &instanceId);
    QString lastButtonIconLayout() const;
    void setLastButtonIconLayout(const QString &layoutId);

    static QString resolveGpuMode();

private:
    SettingsStore() = default;

    QString m_storagePath;
};
