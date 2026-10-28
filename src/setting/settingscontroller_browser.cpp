#include "setting/settingscontroller.h"

#include "browsercontainerquickitem.h"
#include "browsercontroller.h"
#include "siteconfigmanager.h"

void SettingsController::bindBrowserContainer(BrowserContainer *container)
{
    m_browserContainer = container;
    if (!m_browserContainer || !m_siteConfig) {
        return;
    }

    m_browserContainer->setSiteConfigManager(m_siteConfig);
    applyToCurrentUrl();
}

void SettingsController::applyToCurrentUrl()
{
    if (!m_browserContainer || !m_browserController) {
        return;
    }

    const QString url = m_browserController->currentUrl();
    if (url.isEmpty()) {
        return;
    }

    m_browserContainer->applyReadability(url);
}
