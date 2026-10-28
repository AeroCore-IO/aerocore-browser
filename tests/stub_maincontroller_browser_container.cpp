#include "maincontroller.h"

#include "browsercontroller.h"

#include <QImage>

void MainController::notifyBrowserControllerAvailable(BrowserController *controller)
{
    Q_UNUSED(controller);
}

void MainController::onActiveBrowserUrlChanged(const QString &url, const QString &title)
{
    Q_UNUSED(url);
    Q_UNUSED(title);
}

void MainController::onActiveBrowserLoadSucceeded(const QString &url, const QString &title)
{
    Q_UNUSED(url);
    Q_UNUSED(title);
}

void MainController::onNewTabRequested(const QString &url)
{
    Q_UNUSED(url);
}

void MainController::beginPendingSessionSwitch(const QString &url)
{
    Q_UNUSED(url);
}

void MainController::clearPendingSessionSwitch()
{
}

void MainController::recordHistoryVisit(const QString &title, const QString &url)
{
    Q_UNUSED(title);
    Q_UNUSED(url);
}

void MainController::updateHistoryThumbnail(const QString &url, const QImage &image)
{
    Q_UNUSED(url);
    Q_UNUSED(image);
}

void MainController::completeHistoryVisitUiUpdate(const QString &url)
{
    Q_UNUSED(url);
}
