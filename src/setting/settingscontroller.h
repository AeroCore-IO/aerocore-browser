#pragma once

#include <QObject>
#include <QString>

class BrowserContainer;
class BrowserController;
class CefSettingsService;
class InputManager;
class MainController;
class SiteConfigManager;

class SettingsController : public QObject
{
    Q_OBJECT

    Q_PROPERTY(double appUiScale READ appUiScale WRITE setAppUiScale NOTIFY appUiScaleChanged)
    Q_PROPERTY(bool appUiScaleExplicit READ appUiScaleExplicit NOTIFY appUiScaleChanged)
    Q_PROPERTY(double defaultZoomFactor READ defaultZoomFactor WRITE setDefaultZoomFactor NOTIFY defaultsChanged)
    Q_PROPERTY(int defaultMinimumFontSize READ defaultMinimumFontSize WRITE setDefaultMinimumFontSize
                   NOTIFY defaultsChanged)
    Q_PROPERTY(QString readabilitySummary READ readabilitySummary CONSTANT)
    Q_PROPERTY(QString desktopUserAgent READ desktopUserAgent CONSTANT)

    Q_PROPERTY(bool preciseMode READ preciseMode NOTIFY preciseModeChanged)
    Q_PROPERTY(bool turboMode READ turboMode NOTIFY turboModeChanged)
    Q_PROPERTY(QString inputDeviceName READ inputDeviceName NOTIFY inputDeviceNameChanged)

    Q_PROPERTY(double cursorSpeedScale READ cursorSpeedScale WRITE setCursorSpeedScale NOTIFY cursorSpeedScaleChanged)
    Q_PROPERTY(double wheelSpeedScale READ wheelSpeedScale WRITE setWheelSpeedScale NOTIFY wheelSpeedScaleChanged)
    Q_PROPERTY(int cursorSize READ cursorSize WRITE setCursorSize NOTIFY cursorSizeChanged)
    Q_PROPERTY(int cursorAutoHideDelay READ cursorAutoHideDelay WRITE setCursorAutoHideDelay
                   NOTIFY cursorAutoHideDelayChanged)
    Q_PROPERTY(bool edgeScrollEnabled READ edgeScrollEnabled WRITE setEdgeScrollEnabled NOTIFY edgeScrollEnabledChanged)

    Q_PROPERTY(QString gpuMode READ gpuMode WRITE setGpuMode NOTIFY gpuModeChanged)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged)
    Q_PROPERTY(QString cefProfilePath READ cefProfilePath CONSTANT)
    Q_PROPERTY(QString cefLogPath READ cefLogPath CONSTANT)
    Q_PROPERTY(QString appVersion READ appVersion CONSTANT)

public:
    explicit SettingsController(SiteConfigManager *siteConfig,
                                InputManager *inputManager,
                                MainController *mainController,
                                BrowserController *browserController = nullptr,
                                QObject *parent = nullptr);

    double appUiScale() const;
    bool appUiScaleExplicit() const;
    double defaultZoomFactor() const;
    int defaultMinimumFontSize() const;
    QString readabilitySummary() const;
    QString desktopUserAgent() const;

    bool preciseMode() const;
    bool turboMode() const;
    QString inputDeviceName() const;

    double cursorSpeedScale() const;
    double wheelSpeedScale() const;
    int cursorSize() const;
    int cursorAutoHideDelay() const;
    bool edgeScrollEnabled() const;

    QString gpuMode() const;
    bool restartRequired() const;
    QString cefProfilePath() const;
    QString cefLogPath() const;
    QString appVersion() const;

    void setAppUiScale(double factor);
    void setDefaultZoomFactor(double factor);
    void setDefaultMinimumFontSize(int size);

    void setGpuMode(const QString &mode);

    void setCursorSpeedScale(double scale);
    void setWheelSpeedScale(double scale);
    void setCursorSize(int size);
    void setCursorAutoHideDelay(int seconds);
    void setEdgeScrollEnabled(bool enabled);

    Q_INVOKABLE double zoomForUrl(const QString &url) const;
    Q_INVOKABLE int minimumFontForUrl(const QString &url) const;
    Q_INVOKABLE void setReadabilityModeForUrl(const QString &url, const QString &mode);
    Q_INVOKABLE void setZoomForUrl(const QString &url, double factor);
    Q_INVOKABLE void resetSite(const QString &url);
    Q_INVOKABLE void resetAppUiScale();
    Q_INVOKABLE void resetDefaultZoom();
    Q_INVOKABLE void resetDefaultMinimumFontSize();

    Q_INVOKABLE void clearBrowsingHistory();
    Q_INVOKABLE void clearCefCookies();
    Q_INVOKABLE void clearCefCache();

    Q_INVOKABLE void bindBrowserContainer(BrowserContainer *container);
    Q_INVOKABLE void applyToCurrentUrl();

signals:
    void appUiScaleChanged();
    void defaultsChanged();
    void siteConfigChanged(const QString &host);
    void preciseModeChanged();
    void turboModeChanged();
    void inputDeviceNameChanged();
    void cursorSpeedScaleChanged();
    void wheelSpeedScaleChanged();
    void cursorSizeChanged();
    void cursorAutoHideDelayChanged();
    void edgeScrollEnabledChanged();
    void gpuModeChanged();
    void restartRequiredChanged();

private:
    void connectSiteConfigSignals();
    void connectInputManagerSignals();

    SiteConfigManager *m_siteConfig = nullptr;
    InputManager *m_inputManager = nullptr;
    MainController *m_mainController = nullptr;
    BrowserController *m_browserController = nullptr;
    BrowserContainer *m_browserContainer = nullptr;
    CefSettingsService *m_cefSettingsService = nullptr;
    bool m_restartRequired = false;
    QString m_pendingGpuMode;
    int m_cursorSize = 56;
    int m_cursorAutoHideDelay = 3;
};
