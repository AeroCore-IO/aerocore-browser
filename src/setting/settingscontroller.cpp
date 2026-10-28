#include "setting/settingscontroller.h"

#include "browsercontroller.h"
#include "inputmanager.h"
#include "maincontroller.h"
#include "setting/cefsettingsservice.h"
#include "setting/settingsstore.h"
#include "siteconfigmanager.h"

SettingsController::SettingsController(SiteConfigManager *siteConfig,
                                       InputManager *inputManager,
                                       MainController *mainController,
                                       BrowserController *browserController,
                                       QObject *parent)
    : QObject(parent)
    , m_siteConfig(siteConfig)
    , m_inputManager(inputManager)
    , m_mainController(mainController)
    , m_browserController(browserController)
    , m_cefSettingsService(new CefSettingsService(this))
{
    SettingsStore::instance().loadInto(*m_siteConfig);
    m_siteConfig->refreshAutomaticReadability();
    m_pendingGpuMode = SettingsStore::instance().gpuMode();

    m_cursorSize = SettingsStore::instance().cursorSize();
    m_cursorAutoHideDelay = SettingsStore::instance().cursorAutoHideDelay();
    if (m_inputManager) {
        m_inputManager->setCursorSpeedScale(SettingsStore::instance().cursorSpeedScale());
        m_inputManager->setWheelSpeedScale(SettingsStore::instance().wheelSpeedScale());
        m_inputManager->setEdgeScrollEnabled(SettingsStore::instance().edgeScrollEnabled());
    }

    connectSiteConfigSignals();
    connectInputManagerSignals();
}

double SettingsController::appUiScale() const
{
    return m_siteConfig ? m_siteConfig->appUiScale() : 1.0;
}

bool SettingsController::appUiScaleExplicit() const
{
    return m_siteConfig ? m_siteConfig->hasExplicitAppUiScale() : false;
}

double SettingsController::defaultZoomFactor() const
{
    return m_siteConfig ? m_siteConfig->defaultZoomFactor() : 1.2;
}

int SettingsController::defaultMinimumFontSize() const
{
    return m_siteConfig ? m_siteConfig->defaultMinimumFontSize() : 20;
}

QString SettingsController::readabilitySummary() const
{
    return m_siteConfig ? m_siteConfig->automaticReadabilitySummary() : QString();
}

QString SettingsController::desktopUserAgent() const
{
    return m_siteConfig ? m_siteConfig->desktopUserAgent() : QString();
}

bool SettingsController::preciseMode() const
{
    return m_inputManager ? m_inputManager->preciseMode() : false;
}

bool SettingsController::turboMode() const
{
    return m_inputManager ? m_inputManager->turboMode() : false;
}

QString SettingsController::inputDeviceName() const
{
    return m_inputManager ? m_inputManager->inputDeviceName() : QString();
}

double SettingsController::cursorSpeedScale() const
{
    return m_inputManager ? m_inputManager->cursorSpeedScale() : 1.0;
}

double SettingsController::wheelSpeedScale() const
{
    return m_inputManager ? m_inputManager->wheelSpeedScale() : 1.0;
}

int SettingsController::cursorSize() const
{
    return m_cursorSize;
}

int SettingsController::cursorAutoHideDelay() const
{
    return m_cursorAutoHideDelay;
}

bool SettingsController::edgeScrollEnabled() const
{
    return m_inputManager ? m_inputManager->edgeScrollEnabled() : true;
}

QString SettingsController::gpuMode() const
{
    return m_pendingGpuMode;
}

bool SettingsController::restartRequired() const
{
    return m_restartRequired;
}

QString SettingsController::cefProfilePath() const
{
    return m_cefSettingsService->cefProfilePath();
}

QString SettingsController::cefLogPath() const
{
    return m_cefSettingsService->cefLogPath();
}

QString SettingsController::appVersion() const
{
    return m_cefSettingsService->appVersion();
}

void SettingsController::setAppUiScale(double factor)
{
    if (!m_siteConfig) {
        return;
    }

    m_siteConfig->setAppUiScale(factor);
}

void SettingsController::setDefaultZoomFactor(double factor)
{
    if (!m_siteConfig) {
        return;
    }

    m_siteConfig->setDefaultZoomFactor(factor);
}

void SettingsController::setDefaultMinimumFontSize(int size)
{
    if (!m_siteConfig) {
        return;
    }

    m_siteConfig->setDefaultMinimumFontSize(size);
}

void SettingsController::setGpuMode(const QString &mode)
{
    const QString normalized = mode.trimmed().toLower();
    if (normalized.isEmpty() || normalized == m_pendingGpuMode) {
        return;
    }

    m_pendingGpuMode = normalized.isEmpty() ? QStringLiteral("auto") : normalized;
    SettingsStore::instance().setGpuMode(m_pendingGpuMode);

    if (!m_restartRequired) {
        m_restartRequired = true;
        emit restartRequiredChanged();
    }

    emit gpuModeChanged();
}

void SettingsController::setCursorSpeedScale(double scale)
{
    const double bounded = qBound(0.5, scale, 2.0);
    SettingsStore::instance().setCursorSpeedScale(bounded);
    if (m_inputManager) {
        m_inputManager->setCursorSpeedScale(bounded);
    }
    emit cursorSpeedScaleChanged();
}

void SettingsController::setWheelSpeedScale(double scale)
{
    const double bounded = qBound(0.5, scale, 2.0);
    SettingsStore::instance().setWheelSpeedScale(bounded);
    if (m_inputManager) {
        m_inputManager->setWheelSpeedScale(bounded);
    }
    emit wheelSpeedScaleChanged();
}

void SettingsController::setCursorSize(int size)
{
    const int bounded = qBound(16, size, 96);
    if (bounded == m_cursorSize) {
        return;
    }

    m_cursorSize = bounded;
    SettingsStore::instance().setCursorSize(bounded);
    emit cursorSizeChanged();
}

void SettingsController::setCursorAutoHideDelay(int seconds)
{
    const int bounded = qBound(0, seconds, 10);
    if (bounded == m_cursorAutoHideDelay) {
        return;
    }

    m_cursorAutoHideDelay = bounded;
    SettingsStore::instance().setCursorAutoHideDelay(bounded);
    emit cursorAutoHideDelayChanged();
}

void SettingsController::setEdgeScrollEnabled(bool enabled)
{
    SettingsStore::instance().setEdgeScrollEnabled(enabled);
    if (m_inputManager) {
        m_inputManager->setEdgeScrollEnabled(enabled);
    }
    emit edgeScrollEnabledChanged();
}

double SettingsController::zoomForUrl(const QString &url) const
{
    return m_siteConfig ? m_siteConfig->zoomForUrl(url) : 1.2;
}

int SettingsController::minimumFontForUrl(const QString &url) const
{
    return m_siteConfig ? m_siteConfig->minimumFontForUrl(url) : 20;
}

void SettingsController::setReadabilityModeForUrl(const QString &url, const QString &mode)
{
    if (m_siteConfig) {
        m_siteConfig->setReadabilityModeForUrl(url, mode);
    }
}

void SettingsController::setZoomForUrl(const QString &url, double factor)
{
    if (m_siteConfig) {
        m_siteConfig->setZoomForUrl(url, factor);
    }
}

void SettingsController::resetSite(const QString &url)
{
    if (m_siteConfig) {
        m_siteConfig->resetSite(url);
    }
}

void SettingsController::resetAppUiScale()
{
    if (m_siteConfig) {
        m_siteConfig->resetAppUiScale();
    }
}

void SettingsController::resetDefaultZoom()
{
    if (m_siteConfig) {
        m_siteConfig->resetDefaultZoomFactor();
    }
}

void SettingsController::resetDefaultMinimumFontSize()
{
    if (m_siteConfig) {
        m_siteConfig->resetDefaultMinimumFontSize();
    }
}

void SettingsController::clearBrowsingHistory()
{
    if (m_mainController) {
        m_mainController->clearAllHistory();
    }
}

void SettingsController::clearCefCookies()
{
    m_cefSettingsService->clearCefCookies();
}

void SettingsController::clearCefCache()
{
    m_cefSettingsService->clearCefCache();
}

void SettingsController::connectSiteConfigSignals()
{
    if (!m_siteConfig) {
        return;
    }

    connect(m_siteConfig, &SiteConfigManager::appUiScaleChanged, this, [this](double) {
        emit appUiScaleChanged();
    });
    connect(m_siteConfig, &SiteConfigManager::defaultsChanged, this, [this]() {
        emit defaultsChanged();
        applyToCurrentUrl();
    });
    connect(m_siteConfig, &SiteConfigManager::siteConfigChanged, this, [this](const QString &host) {
        emit siteConfigChanged(host);
        applyToCurrentUrl();
    });
}

void SettingsController::connectInputManagerSignals()
{
    if (!m_inputManager) {
        return;
    }

    connect(m_inputManager, &InputManager::preciseModeChanged, this, &SettingsController::preciseModeChanged);
    connect(m_inputManager, &InputManager::turboModeChanged, this, &SettingsController::turboModeChanged);
    connect(m_inputManager, &InputManager::inputDeviceNameChanged, this, &SettingsController::inputDeviceNameChanged);
}
