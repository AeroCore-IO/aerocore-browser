#include "maincontroller.h"

#include "browsercontainerquickitem.h"
#include "browsercontroller.h"
#include "powerinhibitor.h"
#include "setting/settingscontroller.h"

#include <QDebug>
#include <QGuiApplication>

namespace {
QString joinPlaybackReasons(const QStringList &reasons)
{
    return reasons.isEmpty() ? QStringLiteral("none") : reasons.join(QStringLiteral(", "));
}
}

void MainController::bindBrowserContainer(QObject *containerObject)
{
    auto *container = qobject_cast<BrowserContainer *>(containerObject);
    if (!container) {
        return;
    }

    m_browserContainer = container;
    m_browserController = container->browserController();
    container->setMainController(this);
    container->setInputManager(&m_inputManager);

    if (m_settingsController) {
        m_settingsController->bindBrowserContainer(container);
    }

    if (!m_powerInhibitor) {
        m_powerInhibitor = new PowerInhibitor(this);
    }

    connect(container, &BrowserContainer::mediaPlaybackChanged, this, [this](bool) {
        updatePlaybackInhibit();
    });

    if (!m_playbackAppStateConnected) {
        connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState) {
            updatePlaybackInhibit();
        });
        m_playbackAppStateConnected = true;
    }

    ensureBrowserBindings();
    updatePlaybackInhibit();

    if (!m_pendingNavigateUrl.isEmpty()) {
        const QString url = m_pendingNavigateUrl;
        m_pendingNavigateUrl.clear();
        handleNavigateRequested(url);
    }
}

void MainController::loadActiveSessionTabInBrowser(const QString &tabId, const QString &url, bool forceReload)
{
    if (m_browserContainer) {
        m_browserContainer->loadBrowserTab(tabId, url, forceReload);
    }
}

void MainController::markBrowserLoadTiming(const QString &stage)
{
    if (m_browserContainer) {
        m_browserContainer->markLoadTiming(stage);
    }
}

void MainController::closeActiveBrowserPage()
{
    if (m_browserContainer) {
        m_browserContainer->closeActivePage();
    }
}

void MainController::leaveBrowserToHomeIfNeeded()
{
    if (m_browserController && !m_browserController->homeVisible()) {
        closeActiveBrowserPage();
        m_browserController->setPageState(QString(), QString(), false, false);
        m_browserController->goHome();
    }
}

void MainController::enqueueBrowserViewRemovalsForTabs(const QStringList &tabIds)
{
    if (m_browserContainer) {
        m_browserContainer->enqueueBrowserViewRemovals(tabIds);
    }
}

void MainController::notifyBrowserControllerAvailable(BrowserController *controller)
{
    m_browserController = controller;
    ensureBrowserBindings();
}

void MainController::updatePlaybackInhibit()
{
    const bool browserFrontmost = qGuiApp->applicationState() == Qt::ApplicationActive
        && m_browserController
        && !m_browserController->homeVisible()
        && m_browserContainer
        && m_browserContainer->hasActiveView();
    const bool mediaPlaybackActive = browserFrontmost && m_browserContainer->isMediaPlaybackActive();

    QStringList triggerReasons;
    QStringList releaseReasons;
    if (browserFrontmost) {
        if (m_browserContainer->isAudiblePlaybackActive()) {
            triggerReasons.append(QStringLiteral("audio"));
        }
        if (m_browserContainer->isVideoPlaybackActive()) {
            triggerReasons.append(QStringLiteral("video"));
        }
        if (m_browserContainer->isPictureInPicturePlaybackActive()) {
            triggerReasons.append(QStringLiteral("picture-in-picture"));
        }
        if (m_browserContainer->isFullscreenVideoPlaybackActive()) {
            triggerReasons.append(QStringLiteral("fullscreen-video"));
        }
    } else {
        if (qGuiApp->applicationState() != Qt::ApplicationActive) {
            releaseReasons.append(QStringLiteral("app-inactive"));
        }
        if (!m_browserController || m_browserController->homeVisible()) {
            releaseReasons.append(QStringLiteral("home-visible"));
        }
        if (!m_browserContainer || !m_browserContainer->hasActiveView()) {
            releaseReasons.append(QStringLiteral("no-browser-view"));
        }
    }

    const bool shouldInhibit = mediaPlaybackActive;

    if (browserFrontmost && triggerReasons.isEmpty()) {
        releaseReasons.append(QStringLiteral("no-media-playback"));
    }

    const QString logReason = shouldInhibit ? joinPlaybackReasons(triggerReasons) : joinPlaybackReasons(releaseReasons);

    if (shouldInhibit != m_lastPlaybackInhibitActive || logReason != m_lastPlaybackInhibitReason) {
        if (shouldInhibit) {
            qInfo().noquote() << QStringLiteral("Playback inhibit acquired: %1").arg(logReason);
        } else if (m_lastPlaybackInhibitActive) {
            qInfo().noquote() << QStringLiteral("Playback inhibit released: %1").arg(logReason);
        } else {
            qInfo().noquote() << QStringLiteral("Playback inhibit idle: %1").arg(logReason);
        }
        m_lastPlaybackInhibitActive = shouldInhibit;
        m_lastPlaybackInhibitReason = logReason;
    }

    if (m_powerInhibitor) {
        m_powerInhibitor->setActive(shouldInhibit,
                                    QStringLiteral("Foreground browser media playback (%1)").arg(joinPlaybackReasons(triggerReasons)));
    }
}
