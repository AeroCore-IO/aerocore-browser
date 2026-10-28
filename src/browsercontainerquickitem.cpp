#include "browsercontainerquickitem.h"

#include "history/historyitem.h"
#include "inputmanager.h"
#include "maincontroller.h"
#include "siteconfigmanager.h"

#include <QDateTime>
#include <QDebug>
#include <QTimer>

#include <algorithm>
#include <cmath>

BrowserContainer::BrowserContainer(QQuickItem *parent)
    : QQuickItem(parent)
{
    setFlag(QQuickItem::ItemHasContents, false);
    setFlag(QQuickItem::ItemClipsChildrenToShape, true);
    setClip(true);
    setAcceptedMouseButtons(Qt::NoButton);

    m_thumbnailCaptureTimer.setSingleShot(true);
    m_thumbnailCaptureTimer.setInterval(400);
    connect(&m_thumbnailCaptureTimer, &QTimer::timeout, this, &BrowserContainer::onThumbnailCaptureTimeout);
}

BrowserContainer::~BrowserContainer()
{
    disconnectInputManager();
    if (m_inputManager) {
        m_inputManager->setActiveBrowserView(nullptr);
    }
    for (BrowserView *view : std::as_const(m_browserViews)) {
        view->closeCurrentPage();
    }
    m_browserViews.clear();
    m_browserView = nullptr;
}

void BrowserContainer::setBrowserController(BrowserController *controller)
{
    m_browserController = controller;
    if (m_mainController) {
        m_mainController->notifyBrowserControllerAvailable(controller);
    }
}

void BrowserContainer::setInputManager(InputManager *inputManager)
{
    if (m_inputManager == inputManager) {
        return;
    }

    disconnectInputManager();
    if (m_inputManager) {
        m_inputManager->setActiveBrowserView(nullptr);
    }
    m_inputManager = inputManager;
    connectInputManager();
    syncActiveBrowserViewToInputManager();
}

void BrowserContainer::setSiteConfigManager(SiteConfigManager *siteConfig)
{
    m_siteConfig = siteConfig;
}

void BrowserContainer::setMainController(MainController *controller)
{
    m_mainController = controller;
}

void BrowserContainer::applyReadability(const QString &url)
{
    if (!m_browserView || !m_siteConfig) {
        return;
    }

    const double zoom = m_siteConfig->zoomForUrl(url);
    if (qEnvironmentVariableIsSet("AEROCORE_DEBUG_ZOOM_BACK")) {
        qInfo().noquote() << QStringLiteral("[DEBUG-a4f2] applyReadability url=%1 zoomForUrl=%2 currentDesired=%3")
                                 .arg(url)
                                 .arg(zoom, 0, 'f', 3)
                                 .arg(m_browserView->zoomFactor(), 0, 'f', 3);
    }
    m_browserView->setMinimumFontSize(m_siteConfig->minimumFontForUrl(url));
    m_browserView->setZoomFactor(zoom);
}

bool BrowserContainer::atNavigationStart() const
{
    return !m_browserView || !m_browserView->canGoBackNow();
}

void BrowserContainer::notifyAtNavigationStartChanged()
{
    const bool atStart = atNavigationStart();
    if (m_atNavigationStart == atStart) {
        return;
    }

    m_atNavigationStart = atStart;
    emit atNavigationStartChanged();
}

bool BrowserContainer::isLoading() const
{
    return m_browserView && m_browserView->isLoading();
}

bool BrowserContainer::hasActiveView() const
{
    return m_browserView != nullptr;
}

bool BrowserContainer::isAudiblePlaybackActive() const
{
    return m_browserView && m_browserView->isAudiblePlaybackActive();
}

bool BrowserContainer::isVideoPlaybackActive() const
{
    return m_browserView && m_browserView->isVideoPlaybackActive();
}

bool BrowserContainer::isPictureInPicturePlaybackActive() const
{
    return m_browserView && m_browserView->isPictureInPicturePlaybackActive();
}

bool BrowserContainer::isFullscreenVideoPlaybackActive() const
{
    return m_browserView && m_browserView->isFullscreenVideoPlaybackActive();
}

bool BrowserContainer::isMediaPlaybackActive() const
{
    return m_browserView && m_browserView->isMediaPlaybackActive();
}

bool BrowserContainer::hasFrame() const
{
    return m_browserView && m_browserView->hasFrame();
}

quint64 BrowserContainer::beginLoadTiming(const QString &source, const QString &url)
{
    if (!isLoadTimingEnabled()) {
        return 0;
    }

    ++m_loadTimingId;
    m_loadTimingStartMs = QDateTime::currentMSecsSinceEpoch();
    m_loadTimingSource = source;
    m_loadTimingUrl = url;
    logLoadTiming(QStringLiteral("begin"));
    return m_loadTimingId;
}

void BrowserContainer::markLoadTiming(const QString &stage)
{
    if (!isLoadTimingEnabled()) {
        return;
    }
    logLoadTiming(stage);
}

qint64 BrowserContainer::loadTimingElapsedMs() const
{
    if (!isLoadTimingEnabled() || m_loadTimingStartMs <= 0) {
        return -1;
    }
    return QDateTime::currentMSecsSinceEpoch() - m_loadTimingStartMs;
}

bool BrowserContainer::isLoadTimingEnabled() const
{
    return qEnvironmentVariableIsSet("AEROCORE_LOAD_TIMING");
}

void BrowserContainer::logLoadTiming(const QString &stage) const
{
    if (!isLoadTimingEnabled() || m_loadTimingId == 0 || m_loadTimingStartMs <= 0) {
        return;
    }

    const qint64 elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_loadTimingStartMs;
    qInfo().noquote() << QStringLiteral("[load-timing] id=%1 +%2ms stage=%3 source=%4 url=%5 loading=%6 hasFrame=%7 visible=%8 activeTab=%9")
                              .arg(m_loadTimingId)
                              .arg(elapsedMs)
                              .arg(stage, m_loadTimingSource, m_loadTimingUrl)
                              .arg(isLoading() ? QStringLiteral("1") : QStringLiteral("0"))
                              .arg(hasFrame() ? QStringLiteral("1") : QStringLiteral("0"))
                              .arg(isVisible() ? QStringLiteral("1") : QStringLiteral("0"))
                              .arg(m_activeTabId);
}

void BrowserContainer::notifyIsLoadingChanged()
{
    const bool loading = isLoading();
    if (m_isLoading == loading) {
        return;
    }

    m_isLoading = loading;
    logLoadTiming(QStringLiteral("isLoadingChanged → %1").arg(loading ? QStringLiteral("true") : QStringLiteral("false")));
    emit isLoadingChanged();
}

void BrowserContainer::syncActiveBrowserViewToInputManager()
{
    if (!m_inputManager) {
        return;
    }

    m_inputManager->setActiveBrowserView(isVisible() ? m_browserView : nullptr);
}

void BrowserContainer::connectInputManager()
{
    if (!m_inputManager) {
        return;
    }

    m_pointerButtonConnection = connect(
        m_inputManager, &InputManager::pointerButtonRequested,
        this, [this](const QPoint &globalPos, Qt::MouseButton button, bool pressed, Qt::MouseButtons buttons) {
            if (!m_browserView) {
                return;
            }

            m_browserView->sendMouseButtonFromGlobal(globalPos, button, !pressed, buttons);
        });

    m_pointerTouchConnection = connect(
        m_inputManager, &InputManager::pointerTouchRequested,
        this, [this](const QPoint &globalPos, InputManager::PointerTouchEventType eventType) {
            if (!m_browserView) {
                return;
            }

            switch (eventType) {
            case InputManager::PointerTouchEventType::Pressed:
                m_browserView->beginTouchDragFromGlobal(globalPos);
                break;
            case InputManager::PointerTouchEventType::Moved:
                m_browserView->updateTouchDragFromGlobal(globalPos);
                break;
            case InputManager::PointerTouchEventType::Released:
                m_browserView->endTouchDragFromGlobal(globalPos);
                break;
            }
        });

    m_wheelConnection = connect(
        m_inputManager, &InputManager::wheelStepRequested,
        this, [this](const QPoint &globalPos, const QPoint &angleDelta) {
            if (!m_browserView) {
                return;
            }

            m_browserView->syncPointerFromGlobal(globalPos, m_inputManager->heldMouseButtons());
            m_browserView->sendWheelStep(angleDelta);
        });

    m_navigationConnection = connect(
        m_inputManager, &InputManager::navigationKeyRequested,
        this, [this](int key) {
            if (!m_browserView
                || m_inputManager->inputRoute() != InputManager::InputRoute::BrowserPointer
                || m_inputManager->modalOverlayActive()) {
                return;
            }

            m_browserView->sendNavigationKey(key);
        });
}

void BrowserContainer::disconnectInputManager()
{
    if (m_pointerButtonConnection) {
        disconnect(m_pointerButtonConnection);
        m_pointerButtonConnection = {};
    }
    if (m_pointerTouchConnection) {
        disconnect(m_pointerTouchConnection);
        m_pointerTouchConnection = {};
    }
    if (m_wheelConnection) {
        disconnect(m_wheelConnection);
        m_wheelConnection = {};
    }
    if (m_navigationConnection) {
        disconnect(m_navigationConnection);
        m_navigationConnection = {};
    }
}

BrowserView *BrowserContainer::ensureBrowserView(const QString &tabId)
{
    if (tabId.isEmpty()) {
        return nullptr;
    }

    if (BrowserView *existing = m_browserViews.value(tabId, nullptr)) {
        return existing;
    }

    auto *view = new BrowserView(this);
    view->setVisible(false);
    view->setBrowserInputEnabled(false);

    m_browserViews.insert(tabId, view);
    connectBrowserViewSignals(view);
    syncActiveViewGeometry();
    return view;
}

void BrowserContainer::connectBrowserViewSignals(BrowserView *view)
{
    if (!view) {
        return;
    }

    connect(view, &BrowserView::addressChanged, this, [this, view](const QString &url) {
        if (view != m_browserView || !m_browserController) {
            return;
        }

        m_browserController->setPageState(
            url,
            view->currentTitle(),
            view->canGoBack(),
            view->canGoForward());
        notifyAtNavigationStartChanged();
        applyReadability(url);
        if (m_mainController) {
            m_mainController->onActiveBrowserUrlChanged(url, view->currentTitle());
        }
    });

    connect(view, &BrowserView::titleChanged, this, [this, view](const QString &title) {
        if (view != m_browserView || !m_browserController) {
            return;
        }

        m_browserController->setPageState(
            view->currentUrl(),
            title,
            view->canGoBack(),
            view->canGoForward());
        notifyAtNavigationStartChanged();
        if (m_mainController) {
            m_mainController->onActiveBrowserUrlChanged(view->currentUrl(), title);
        }
    });

    connect(view, &BrowserView::navigationStateChanged, this, [this, view](bool canGoBack, bool canGoForward) {
        if (view != m_browserView || !m_browserController) {
            return;
        }

        m_browserController->setPageState(
            view->currentUrl(),
            view->currentTitle(),
            canGoBack,
            canGoForward);
        notifyAtNavigationStartChanged();
    });

    connect(view, &BrowserView::loadingStateChanged, this, [this, view](bool) {
        if (view != m_browserView) {
            return;
        }

        notifyIsLoadingChanged();
    });

    connect(view, &BrowserView::loadSucceeded, this, [this, view](const QString &url, const QString &title) {
        if (view != m_browserView) {
            return;
        }

        if (m_mainController) {
            m_mainController->onActiveBrowserLoadSucceeded(url, title);
            m_mainController->recordHistoryVisit(title, url);
        }

        m_pendingThumbnailUrl = url;
        m_thumbnailCaptureTimer.start();
    });

    connect(view, &BrowserView::loadFailed, this, [this, view](const QString &url, int, const QString &errorText) {
        if (view != m_browserView) {
            return;
        }

        notifyIsLoadingChanged();
        emit loadFailed(url, errorText);
    });

    connect(view, &BrowserView::newTabRequested, this, [this, view](const QString &url) {
        if (view != m_browserView || !m_mainController) {
            return;
        }

        m_mainController->onNewTabRequested(url);
    });

    connect(view, &BrowserView::renderProcessTerminated, this,
            [this, view](int status, int errorCode, const QString &errorText) {
                const QString message = QStringLiteral("Renderer crashed (status=%1, code=%2): %3")
                                            .arg(status)
                                            .arg(errorCode)
                                            .arg(errorText.isEmpty() ? QStringLiteral("no additional details")
                                                                     : errorText);
                if (view == m_browserView) {
                    emit renderProcessCrashed(message);
                }
                // Always reload — even for background views — so the renderer
                // is alive when the user switches back to this tab.
                view->reloadPage();
            });

    connect(view, &BrowserView::fullscreenModeChanged, this, [this, view](bool fullscreen) {
        if (view != m_browserView || !m_inputManager) {
            return;
        }

        m_inputManager->setBrowserFullscreenActive(fullscreen);
    });

    connect(view, &BrowserView::firstFrameAvailable, this, [this, view]() {
        if (view == m_browserView) {
            logLoadTiming(QStringLiteral("firstFrameAvailable"));
            emit activeFrameAvailable();
            logLoadTiming(QStringLiteral("activeFrameAvailable(from-paint)"));
        }
    });

    connect(view, &BrowserView::mediaPlaybackChanged, this, [this, view](bool) {
        if (view != m_browserView) {
            return;
        }

        emit mediaPlaybackChanged(isMediaPlaybackActive());
    });

    connect(view, &BrowserView::virtualKeyboardRequested, this, [this, view](bool requested) {
        qInfo().noquote() << QStringLiteral("BrowserContainer: virtualKeyboardRequested=%1 active=%2 homeVisible=%3")
                                 .arg(requested ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(view == m_browserView ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(m_browserController && m_browserController->homeVisible() ? QStringLiteral("true") : QStringLiteral("false"));
        if (view != m_browserView || !m_inputManager) {
            return;
        }
        if (!requested) {
            m_inputManager->closeWebKeyboardSession();
            return;
        }
        if ((m_browserController && m_browserController->homeVisible()) || m_inputManager->modalOverlayActive()) {
            return;
        }
        view->forceActiveFocus(Qt::OtherFocusReason);
        m_inputManager->openWebKeyboardSession(view);
    });
}

void BrowserContainer::showBrowserTab(const QString &tabId)
{
    BrowserView *view = ensureBrowserView(tabId);
    if (!view) {
        return;
    }

    m_activeTabId = tabId;
    m_browserView = view;

    for (BrowserView *other : m_browserViews) {
        if (other != view) {
            other->setBrowserInputEnabled(false);
            other->setFocus(false);
            other->setVisible(false);   // rendering: skip painting inactive tabs
            other->setTabActive(false); // lifecycle: WasHidden(true), master parity for tab switch
            // Living-room rule: only the visible tab may produce audio.
            other->setAudioMuted(true);
        }
    }

    // Living-room rule: audio only while the container is on-screen.
    view->setAudioMuted(!isVisible());

    syncActiveViewGeometry();

    if (m_browserController) {
        m_browserController->showBrowser();
    }
    if (m_inputManager) {
        m_inputManager->setBrowserFullscreenActive(view->isFullscreenMode());
    }

    syncBrowserControllerFromView(view);
    notifyIsLoadingChanged();

    // Activate the tab regardless of container visibility: the browser must
    // be created and warm even while the page is hidden (master parity — the
    // QWidget branch never stopped compositors on page-level hides).
    view->setVisible(true);   // rendering: paint the active tab
    view->setTabActive(true); // lifecycle: WasHidden(false), create browser, arm watchdog
    if (isVisible()) {
        view->setBrowserInputEnabled(true);
        view->requestNativeFocus();
    }

    syncActiveBrowserViewToInputManager();

    emit mediaPlaybackChanged(isMediaPlaybackActive());
}

void BrowserContainer::syncBrowserControllerFromView(BrowserView *view)
{
    if (!m_browserController) {
        return;
    }

    if (!view) {
        m_browserController->setPageState(QString(), QString(), false, false);
        notifyAtNavigationStartChanged();
        return;
    }

    m_browserController->setPageState(
        view->currentUrl(),
        view->currentTitle(),
        view->canGoBack(),
        view->canGoForward());
    notifyAtNavigationStartChanged();
    if (!view->currentUrl().trimmed().isEmpty()) {
        applyReadability(view->currentUrl());
    }
}

void BrowserContainer::loadBrowserTab(const QString &tabId, const QString &url, bool forceReload)
{
    const qint64 showStartMs = QDateTime::currentMSecsSinceEpoch();
    showBrowserTab(tabId);
    logLoadTiming(QStringLiteral("showBrowserTab done +%1ms tab=%2")
                      .arg(QDateTime::currentMSecsSinceEpoch() - showStartMs)
                      .arg(tabId));
    if (!m_browserView) {
        logLoadTiming(QStringLiteral("loadBrowserTab abort: no view"));
        return;
    }

    const QString targetUrl = url.trimmed();
    const QString currentUrl = m_browserView->currentUrl().trimmed();
    const bool needsLoad = !targetUrl.isEmpty()
        && (forceReload || currentUrl.isEmpty() || currentUrl != targetUrl);

    logLoadTiming(QStringLiteral("loadBrowserTab decide needsLoad=%1 forceReload=%2 currentUrl=%3 targetUrl=%4 view=%5x%6")
                      .arg(needsLoad ? QStringLiteral("1") : QStringLiteral("0"))
                      .arg(forceReload ? QStringLiteral("1") : QStringLiteral("0"))
                      .arg(currentUrl, targetUrl)
                      .arg(qRound(m_browserView->width()))
                      .arg(qRound(m_browserView->height())));

    if (needsLoad) {
        if (m_mainController) {
            m_mainController->beginPendingSessionSwitch(targetUrl);
        }
        // Apply zoom / min-font before CreateBrowserSync so the first navigation
        // does not start at BrowserView defaults (1.2 / 20).
        const qint64 readabilityStartMs = QDateTime::currentMSecsSinceEpoch();
        applyReadability(targetUrl);
        logLoadTiming(QStringLiteral("applyReadability +%1ms")
                          .arg(QDateTime::currentMSecsSinceEpoch() - readabilityStartMs));
        // loadUrl keeps any cached frame visible and arms next-paint notification
        // so the spinner can drop on the first fresh OSR frame (not full load).
        const qint64 loadStartMs = QDateTime::currentMSecsSinceEpoch();
        m_browserView->loadUrl(targetUrl);
        logLoadTiming(QStringLiteral("loadUrl returned +%1ms")
                          .arg(QDateTime::currentMSecsSinceEpoch() - loadStartMs));
    } else if (m_mainController) {
        m_mainController->clearPendingSessionSwitch();
        syncBrowserControllerFromView(m_browserView);
    }

    // Warm resume keeps the cached OSR frame and never re-emits
    // firstFrameAvailable (viewImage already non-null). Tell QML immediately
    // so the homepage loading spinner can dismiss without waiting on a poll.
    if (!needsLoad && hasFrame()) {
        logLoadTiming(QStringLiteral("warm-resume emit activeFrameAvailable"));
        emit activeFrameAvailable();
        logLoadTiming(QStringLiteral("warm-resume activeFrameAvailable emitted"));
    } else if (!needsLoad) {
        logLoadTiming(QStringLiteral("warm-path but no cached frame"));
    }

    if (isVisible()) {
        syncActiveViewGeometry();
        m_browserView->setBrowserInputEnabled(true);
    }
    logLoadTiming(QStringLiteral("loadBrowserTab end"));
}

void BrowserContainer::enqueueBrowserViewRemovals(const QStringList &tabIds)
{
    bool queuedAny = false;
    for (const QString &tabId : tabIds) {
        if (tabId.isEmpty() || m_pendingBrowserViewRemovals.contains(tabId)) {
            continue;
        }

        m_pendingBrowserViewRemovals.append(tabId);
        queuedAny = true;
    }

    if (!queuedAny || m_browserViewRemovalScheduled) {
        return;
    }

    m_browserViewRemovalScheduled = true;
    QTimer::singleShot(0, this, &BrowserContainer::processPendingBrowserViewRemoval);
}

void BrowserContainer::processPendingBrowserViewRemoval()
{
    if (m_pendingBrowserViewRemovals.isEmpty()) {
        m_browserViewRemovalScheduled = false;
        return;
    }

    const QString tabId = m_pendingBrowserViewRemovals.takeFirst();
    removeBrowserView(tabId);

    if (m_pendingBrowserViewRemovals.isEmpty()) {
        m_browserViewRemovalScheduled = false;
        return;
    }

    QTimer::singleShot(0, this, &BrowserContainer::processPendingBrowserViewRemoval);
}

void BrowserContainer::removeBrowserView(const QString &tabId)
{
    BrowserView *view = m_browserViews.take(tabId);
    if (!view) {
        return;
    }

    if (view == m_browserView) {
        m_browserView = nullptr;
        m_activeTabId.clear();
        notifyAtNavigationStartChanged();
        notifyIsLoadingChanged();
        syncActiveBrowserViewToInputManager();
        emit mediaPlaybackChanged(false);
    }

    const auto finalizeRemoval = [view]() {
        // Drop window association while BrowserView is still fully alive so
        // ~QQuickItem does not emit windowChanged into a half-destroyed object.
        view->setParentItem(nullptr);
        view->deleteLater();
    };

    if (!view->hasLiveBrowser() && !view->isBrowserClosing()) {
        finalizeRemoval();
        return;
    }

    connect(view, &BrowserView::browserClosed, this, finalizeRemoval, Qt::SingleShotConnection);
    view->closeCurrentPage();
}

void BrowserContainer::closeActivePage()
{
    if (m_browserView) {
        m_browserView->closeCurrentPage();
    }
}

void BrowserContainer::onThumbnailCaptureTimeout()
{
    if (m_pendingThumbnailUrl.isEmpty()) {
        return;
    }

    const QString url = m_pendingThumbnailUrl;
    m_pendingThumbnailUrl.clear();
    captureHistoryThumbnailForUrl(url);
}

void BrowserContainer::captureHistoryThumbnailForUrl(const QString &url, int attempt)
{
    if (!m_browserView || !m_mainController) {
        return;
    }

    constexpr int kMaxAttempts = 5;
    constexpr int kRetryIntervalMs = 100;

    if (normalizeHistoryUrl(m_browserView->currentUrl()) != normalizeHistoryUrl(url)) {
        m_mainController->completeHistoryVisitUiUpdate(url);
        return;
    }

    const QImage shot = m_browserView->captureViewImage();
    if (shot.isNull()) {
        if (attempt + 1 < kMaxAttempts) {
            QTimer::singleShot(kRetryIntervalMs, this, [this, url, attempt]() {
                captureHistoryThumbnailForUrl(url, attempt + 1);
            });
            return;
        }

        m_mainController->completeHistoryVisitUiUpdate(url);
        return;
    }

    m_mainController->updateHistoryThumbnail(url, shot);
    m_mainController->completeHistoryVisitUiUpdate(url);
}

void BrowserContainer::goBack()
{
    if (m_browserView && m_browserView->canGoBackNow()) {
        if (qEnvironmentVariableIsSet("AEROCORE_DEBUG_ZOOM_BACK")) {
            qInfo().noquote() << QStringLiteral("[DEBUG-a4f2] BrowserContainer::goBack url=%1 zoom=%2")
                                     .arg(m_browserView->currentUrl())
                                     .arg(m_browserView->zoomFactor(), 0, 'f', 3);
        }
        m_browserView->goBack();
    }
}

void BrowserContainer::goForward()
{
    if (m_browserView && m_browserView->canGoForwardNow()) {
        m_browserView->goForward();
    }
}

void BrowserContainer::reloadPage()
{
    if (m_browserView) {
        m_browserView->reloadPage();
    }
}

void BrowserContainer::adjustPageZoom(double delta)
{
    if (!m_browserView || !m_siteConfig || !m_browserController) {
        return;
    }

    const QString url = m_browserController->currentUrl();
    if (url.isEmpty()) {
        return;
    }

    const double currentZoom = m_siteConfig->zoomForUrl(url);
    const double nextZoom = std::clamp(currentZoom + delta, 0.8, 3.0);
    if (qFuzzyCompare(currentZoom, nextZoom)) {
        return;
    }

    m_siteConfig->setZoomForUrl(url, nextZoom);
    m_browserView->setZoomFactor(nextZoom);
}

void BrowserContainer::adjustMinimumFontSize(int delta)
{
    if (!m_browserView || !m_siteConfig || !m_browserController) {
        return;
    }

    const QString url = m_browserController->currentUrl();
    if (url.isEmpty()) {
        return;
    }

    const int currentSize = m_siteConfig->minimumFontForUrl(url);
    const int nextSize = std::clamp(currentSize + delta, 12, 28);
    if (currentSize == nextSize) {
        return;
    }

    applyMinimumFontSize(nextSize);
}

void BrowserContainer::applyMinimumFontSize(int size)
{
    if (!m_browserView || !m_siteConfig || !m_browserController) {
        return;
    }

    const QString url = m_browserController->currentUrl();
    if (url.isEmpty()) {
        return;
    }

    const int nextSize = std::clamp(size, 12, 28);
    m_siteConfig->setMinimumFontSizeForUrl(url, nextSize);
    m_browserView->setMinimumFontSize(nextSize);
}

void BrowserContainer::syncActiveViewGeometry()
{
    const qreal sf = std::max<qreal>(m_scaleFactor, 0.01);
    const QSizeF cefSize(std::ceil(width() * sf), std::ceil(height() * sf));

    for (BrowserView *view : m_browserViews) {
        view->setPosition(QPointF(0, 0));
        view->setSize(cefSize);
        view->setTransformOrigin(QQuickItem::TopLeft);
        view->setScale(1.0 / sf);
        view->setUiScaleFactor(sf);
    }
}

void BrowserContainer::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        syncActiveViewGeometry();
    }
}

void BrowserContainer::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickItem::itemChange(change, value);

    if (change != ItemVisibleHasChanged) {
        return;
    }

    logLoadTiming(QStringLiteral("container ItemVisibleHasChanged → %1")
                      .arg(isVisible() ? QStringLiteral("visible") : QStringLiteral("hidden")));

    // Design note (parity with the QWidget master branch): page-level
    // visibility is deliberately NOT propagated to the BrowserViews. In
    // master, hiding the browser page never delivered hideEvent to the
    // views, so their OSR compositors kept running and frames kept flowing.
    // The QML port originally forwarded setVisible(false) here, which added
    // WasHidden(true/false) cycles that master never had. A view's own
    // visibility now only means "this is the active tab"; Qt Quick still
    // skips rendering the whole subtree while the container is hidden, so
    // there is no painting cost. (The actual pure-background bug was a
    // homeVisible desync in the LT/RT path — fixed in MainWindow.qml
    // switchPage — not a WasHidden stall; this stays as master-parity
    // hardening.)
    const bool show = isVisible();
    for (BrowserView *view : m_browserViews) {
        const bool active = show && view == m_browserView;
        view->setBrowserInputEnabled(active);
        if (!active) {
            view->setFocus(false);
        }
        // Living-room rule: only the on-screen active tab may produce audio.
        // When the container hides (user navigates to the home page) every
        // tab is muted; when it re-appears only the active tab is unmuted.
        view->setAudioMuted(!active);
    }

    if (show) {
        syncActiveViewGeometry();
        if (m_browserView) {
            // Repaint the cached frame so content appears immediately even
            // before the next OSR frame arrives.
            m_browserView->update();
            m_browserView->requestNativeFocus();
        }
    }
    syncActiveBrowserViewToInputManager();
}

void BrowserContainer::setScaleFactor(qreal factor)
{
    const qreal bounded = std::max<qreal>(factor, 0.01);
    if (qFuzzyCompare(m_scaleFactor, bounded)) {
        return;
    }

    m_scaleFactor = bounded;
    syncActiveViewGeometry();
}
