#include "maincontroller.h"

void MainController::bindBrowserContainer(QObject *containerObject)
{
    m_browserContainer = reinterpret_cast<BrowserContainer *>(containerObject);
}

void MainController::notifyBrowserControllerAvailable(BrowserController *controller)
{
    m_browserController = controller;
}

void MainController::loadActiveSessionTabInBrowser(const QString &tabId, const QString &url, bool forceReload)
{
    Q_UNUSED(tabId);
    Q_UNUSED(url);
    Q_UNUSED(forceReload);
}

void MainController::markBrowserLoadTiming(const QString &stage)
{
    Q_UNUSED(stage);
}

void MainController::closeActiveBrowserPage()
{
}

void MainController::leaveBrowserToHomeIfNeeded()
{
}

void MainController::enqueueBrowserViewRemovalsForTabs(const QStringList &tabIds)
{
    Q_UNUSED(tabIds);
}

void MainController::updatePlaybackInhibit()
{
}
