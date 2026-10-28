#include "mainwindow.h"

#include "bookmarkmodel.h"
#include "browsercontroller.h"
#include "browserview.h"
#include "cursorutils.h"
#include "historymodel.h"
#include "inputmanager.h"
#include "powerinhibitor.h"
#include "sessionmanager.h"
#include "siteconfigmanager.h"

#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMessageBox>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QAbstractButton>
#include <QApplication>
#include <QPixmap>
#include <QScreen>
#include <QTimer>
#include <QStackedLayout>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {
constexpr int kCustomCursorIdleHideMs = 3000;

int dpadKeyForKeyboard(int key)
{
    switch (key) {
    case Qt::Key_Up:
        return Qt::Key_Up;
    case Qt::Key_Left:
        return Qt::Key_Left;
    case Qt::Key_Down:
        return Qt::Key_Down;
    case Qt::Key_Right:
        return Qt::Key_Right;
    default:
        return 0;
    }
}


QPoint cursorDeltaForKeyboard(int key, Qt::KeyboardModifiers modifiers)
{
    constexpr int kKeyboardCursorStep = 48;
    constexpr int kKeyboardTurboCursorStep = 96;
    const int step = modifiers.testFlag(Qt::ShiftModifier) ? kKeyboardTurboCursorStep : kKeyboardCursorStep;

    switch (key) {
    case Qt::Key_W:
        return QPoint(0, -step);
    case Qt::Key_S:
        return QPoint(0, step);
    case Qt::Key_A:
        return QPoint(-step, 0);
    case Qt::Key_D:
        return QPoint(step, 0);
    default:
        return QPoint();
    }
}

QString joinPlaybackReasons(const QStringList &reasons)
{
    return reasons.isEmpty() ? QStringLiteral("none") : reasons.join(QStringLiteral(", "));
}

struct UiScaleReading
{
    qreal value = 1.0;
    QString source = QStringLiteral("display fallback");
};

UiScaleReading detectSystemUiScale(const QWidget *widget)
{
    const QScreen *screen = widget ? widget->screen() : QGuiApplication::primaryScreen();
    if (!screen) {
        return {};
    }

    const qreal compositorScale = widget ? widget->devicePixelRatioF() : screen->devicePixelRatio();
    if (compositorScale > 1.01) {
        return {compositorScale, QStringLiteral("Qt/compositor")};
    }

    const qreal logicalDpiScale = screen->logicalDotsPerInch() > 1.0
        ? screen->logicalDotsPerInch() / 96.0
        : 1.0;
    if (logicalDpiScale > 1.01) {
        return {logicalDpiScale, QStringLiteral("logical DPI")};
    }

    const QRect geometry = screen->geometry();
    const qreal widthScale = geometry.width() > 0 ? geometry.width() / 1920.0 : 1.0;
    const qreal heightScale = geometry.height() > 0 ? geometry.height() / 1080.0 : 1.0;
    const qreal resolutionScale = std::sqrt(std::max(widthScale, heightScale));

    qreal referenceDpi = screen->logicalDotsPerInch();
    const qreal physicalDpi = screen->physicalDotsPerInch();
    if (physicalDpi > 1.0) {
        referenceDpi = std::min(referenceDpi, physicalDpi);
    }
    if (referenceDpi <= 1.0) {
        referenceDpi = 96.0;
    }

    const qreal densityScale = 96.0 / referenceDpi;
    return {std::max({1.0, resolutionScale, densityScale}), QStringLiteral("display fallback")};
}

QSize currentDisplayResolution(const QWidget *widget)
{
    const QScreen *screen = widget ? widget->screen() : QGuiApplication::primaryScreen();
    if (!screen) {
        return QSize();
    }

    const QRect geometry = screen->geometry();
    const qreal devicePixelRatio = widget ? widget->devicePixelRatioF() : screen->devicePixelRatio();
    return QSize(qMax(0, qRound(geometry.width() * devicePixelRatio)),
                 qMax(0, qRound(geometry.height() * devicePixelRatio)));
}

QString elideHudLines(const QString &header,
                     const QStringList &lines,
                     const QFontMetrics &metrics,
                     int width,
                     int maxBodyLines)
{
    QStringList result;
    result.append(metrics.elidedText(header, Qt::ElideRight, width));

    const int visibleBodyLines = qMax(0, maxBodyLines);
    for (int index = 0; index < qMin(lines.size(), visibleBodyLines); ++index) {
        result.append(metrics.elidedText(lines.at(index), Qt::ElideRight, width));
    }

    if (lines.size() > visibleBodyLines && !result.isEmpty()) {
        result.last() = metrics.elidedText(result.last() + QStringLiteral("  ..."), Qt::ElideRight, width);
    }

    return result.join(QLatin1Char('\n'));
}

QWidget *dialogLikeOverlayAncestor(QWidget *widget)
{
    while (widget) {
        if (widget->property("dialogLikeOverlay").toBool()) {
            return widget;
        }
        widget = widget->parentWidget();
    }

    return nullptr;
}

QWidget *activeModalDialogWidget()
{
    QWidget *widget = QApplication::activeModalWidget();
    if (widget && qobject_cast<QDialog *>(widget)) {
        return widget;
    }

    if (QWidget *overlay = dialogLikeOverlayAncestor(QApplication::focusWidget())) {
        return overlay;
    }

    const QWidgetList widgets = QApplication::allWidgets();
    for (QWidget *candidate : widgets) {
        if (candidate && candidate->isVisible() && candidate->property("dialogLikeOverlay").toBool()) {
            return candidate;
        }
    }

    return nullptr;
}

void cycleDialogFocus(QWidget *dialog, bool forward)
{
    QWidget *target = QApplication::focusWidget();
    if (!target || !dialog->isAncestorOf(target)) {
        target = dialog;
    }

    const int key = forward ? Qt::Key_Tab : Qt::Key_Backtab;
    const Qt::KeyboardModifiers modifiers = forward ? Qt::NoModifier : Qt::ShiftModifier;
    QKeyEvent pressEvent(QEvent::KeyPress, key, modifiers);
    QKeyEvent releaseEvent(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(target, &pressEvent);
    QCoreApplication::sendEvent(target, &releaseEvent);
}

QString qtScaleSource()
{
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
        return QStringLiteral("QT_SCALE_FACTOR");
    }
    if (qEnvironmentVariableIsSet("QT_SCREEN_SCALE_FACTORS")) {
        return QStringLiteral("QT_SCREEN_SCALE_FACTORS");
    }
    if (qEnvironmentVariableIsSet("QT_FONT_DPI")) {
        return QStringLiteral("QT_FONT_DPI");
    }
    return QStringLiteral("system");
}

class CursorOverlay final : public QWidget
{
public:
    explicit CursorOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void updateCursorFrame(const QPixmap &pixmap,
                           const QPoint &hotspot,
                           const QPoint &globalPos,
                           bool visible)
    {
        const bool pixmapChanged = m_pixmap.cacheKey() != pixmap.cacheKey();
        const bool changed = pixmapChanged
            || m_hotspot != hotspot
            || m_globalPos != globalPos
            || m_visible != visible;

        m_pixmap = pixmap;
        m_hotspot = hotspot;
        m_globalPos = globalPos;
        m_visible = visible;

        setVisible(visible);
        if (changed) {
            update();
        }
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        if (!m_visible || m_pixmap.isNull()) {
            return;
        }

        QPainter painter(this);
        const QPoint localTopLeft = mapFromGlobal(m_globalPos) - m_hotspot;
        painter.drawPixmap(localTopLeft, m_pixmap);
    }

private:
    QPixmap m_pixmap;
    QPoint m_hotspot;
    QPoint m_globalPos;
    bool m_visible = false;
};
}

int MainWindow::scaledPx(int value) const
{
    return qMax(1, qRound(value * (m_siteConfig ? m_siteConfig->appUiScale() : 1.0)));
}

QListWidgetItem *MainWindow::makeListItem(const QString &title, const QString &detail) const
{
    auto *item = new QListWidgetItem(QStringLiteral("%1\n%2").arg(title, detail));
    item->setSizeHint(QSize(scaledPx(320), scaledPx(72)));
    return item;
}

void MainWindow::applyAppUiScale()
{
    const int baseFont = scaledPx(18);
    const int borderWidth = scaledPx(2);
    const int radius = scaledPx(14);
    const int paddingY = scaledPx(12);
    const int paddingX = scaledPx(16);
    const int buttonMinHeight = scaledPx(56);
    const int lineEditMinHeight = scaledPx(52);
    const int overlayButtonMinHeight = scaledPx(48);
    const int overlayLineEditMinHeight = scaledPx(48);
    const int overlayButtonPaddingY = scaledPx(6);
    const int overlayButtonPaddingX = scaledPx(14);
    const int overlayButtonFont = scaledPx(16);
    const int titleFont = scaledPx(40);
    const int subtitleFont = scaledPx(20);
    const int hudTitleFont = scaledPx(26);
    const int hudPadding = scaledPx(24);
    const int hudSpacing = scaledPx(8);
    qApp->setStyleSheet(QStringLiteral(
        "QWidget { background: #070b10; color: #f0f5fa; font-size: %1px; }"
        "QMainWindow, QDialog { background: #070b10; }"
        "QPushButton, QListWidget, QLineEdit { border: %2px solid #223244; border-radius: %3px; padding: %4px %5px; background: #101a25; }"
        "QPushButton { min-height: %6px; }"
        "QLineEdit { min-height: %7px; }"
        "QPushButton:hover { border-color: #385979; background: #122233; }"
        "QPushButton[homeTopButton='true']:hover { border-color: #223244; background: #101a25; }"
        "QPushButton:focus, QListWidget:focus, QListWidget::item:selected, QLineEdit:focus { border-color: #4ea1ff; background: #142437; }"
        "QListWidget[historyDialogList='true']::item { margin: 2px 0; padding: %4px %5px; border: 1px solid transparent; border-radius: %3px; }"
        "QListWidget[historyDialogList='true']::item:selected { background: #173049; border: %2px solid #7fc0ff; color: #f0f5fa; }"
        "QPushButton[homeSectionActive='true'], QListWidget[homeSectionActive='true'] { border-color: #31506f; background: #0d1823; }"
        "QPushButton[homeTopButton='true'][homeSectionActive='true']:hover { border-color: #31506f; background: #0d1823; }"
        "QPushButton[homeSectionActive='true']:focus, QListWidget[homeSectionActive='true']:focus { border-color: #7fc0ff; background: #173049; }"
        "QLabel[role='muted'] { color: #99a8b7; }"
        "QLabel[role='title'] { font-size: %8px; font-weight: 700; }"
        "QLabel[role='subtitleMuted'] { font-size: %9px; color: #99a8b7; }"
        "QLabel[role='hudTitle'] { font-size: %10px; font-weight: 700; }")
                         .arg(baseFont)
                         .arg(borderWidth)
                         .arg(radius)
                         .arg(paddingY)
                         .arg(paddingX)
                         .arg(buttonMinHeight)
                         .arg(lineEditMinHeight)
                         .arg(titleFont)
                         .arg(subtitleFont)
                         .arg(hudTitleFont));

    if (m_titleLabel) {
        m_titleLabel->setProperty("role", "hudTitle");
        m_titleLabel->style()->unpolish(m_titleLabel);
        m_titleLabel->style()->polish(m_titleLabel);
    }

    if (m_hud) {
        if (auto *hudLayout = qobject_cast<QVBoxLayout *>(m_hud->layout())) {
            hudLayout->setContentsMargins(hudPadding, hudPadding, hudPadding, hudPadding);
            hudLayout->setSpacing(hudSpacing);
        }
    }

    const auto applyOverlayControlMetrics = [overlayButtonMinHeight,
                                             overlayLineEditMinHeight,
                                             overlayButtonPaddingY,
                                             overlayButtonPaddingX,
                                             overlayButtonFont](QWidget *panel) {
        if (!panel) {
            return;
        }

        const QList<QPushButton *> buttons = panel->findChildren<QPushButton *>();
        for (QPushButton *button : buttons) {
            button->setStyleSheet(
                QStringLiteral("min-height: %1px; padding: %2px %3px; font-size: %4px;")
                    .arg(overlayButtonMinHeight)
                    .arg(overlayButtonPaddingY)
                    .arg(overlayButtonPaddingX)
                    .arg(overlayButtonFont));
        }

        const QList<QLineEdit *> lineEdits = panel->findChildren<QLineEdit *>();
        for (QLineEdit *lineEdit : lineEdits) {
            lineEdit->setStyleSheet(
                QStringLiteral("min-height: %1px; padding: %2px %3px; font-size: %4px;")
                    .arg(overlayLineEditMinHeight)
                    .arg(overlayButtonPaddingY)
                    .arg(overlayButtonPaddingX)
                    .arg(overlayButtonFont));
        }
    };

    applyOverlayControlMetrics(m_addressPanel);
    applyOverlayControlMetrics(m_bookmarkPanel);
    applyOverlayControlMetrics(m_historyPanel);
    applyOverlayControlMetrics(m_settingsPanel);
    applyOverlayControlMetrics(m_exitPanel);

    refreshBookmarkList();
    refreshSessionList();
    updateHomeSectionVisualState();
    updateHud();
}

BrowserView *MainWindow::ensureBrowserView(const QString &tabId)
{
    if (tabId.isEmpty()) {
        return nullptr;
    }

    if (BrowserView *existing = m_browserViews.value(tabId, nullptr)) {
        return existing;
    }

    auto *view = new BrowserView(m_browserStack);
    m_browserStack->addWidget(view);
    m_browserViews.insert(tabId, view);

    connect(view, &BrowserView::tabPressed, this, [this, view]() {
        if (view != m_browserView || m_browserController->homeVisible() || activeModalDialogWidget()) {
            return;
        }

        showHudTemporarily(1000);
    });

    connect(view, &BrowserView::addressChanged, this, [this, view](const QString &url) {
        if (view != m_browserView) {
            return;
        }

        m_browserController->setPageState(url, view->currentTitle(), view->canGoBack(), view->canGoForward());
        if (shouldApplySessionUpdate(url)) {
            m_sessionManager->updateActiveSession(url, view->currentTitle());
        }
        syncBookmarkCursor(url);
        applyBrowserReadability(view, url);
        updateHud();
    });

    connect(view, &BrowserView::titleChanged, this, [this, view](const QString &title) {
        if (view != m_browserView) {
            return;
        }

        if (shouldApplySessionUpdate(view->currentUrl())) {
            m_sessionManager->updateActiveSession(view->currentUrl(), title);
        }
        m_browserController->setPageState(view->currentUrl(), title, view->canGoBack(), view->canGoForward());
        updateHud();
    });

    connect(view, &BrowserView::navigationStateChanged, this, [this, view](bool canGoBack, bool canGoForward) {
        if (view != m_browserView) {
            return;
        }

        m_browserController->setPageState(view->currentUrl(), view->currentTitle(), canGoBack, canGoForward);
        updateHud();
    });

    connect(view, &BrowserView::loadSucceeded, this, [this, view](const QString &url, const QString &title) {
        if (view != m_browserView) {
            return;
        }

        if (shouldApplySessionUpdate(url)) {
            m_sessionManager->updateActiveSession(url, title);
            m_historyModel->addVisit(title, url);
            clearPendingSessionSwitch();
        }
        updateHud();
    });

    connect(view, &BrowserView::newTabRequested, this, [this, view](const QString &url) {
        if (view != m_browserView || m_browserController->homeVisible()) {
            return;
        }

        const QString tabUrl = m_sessionManager->openUrlInNewTab(url, url);
        if (tabUrl.isEmpty()) {
            return;
        }

        syncBookmarkCursor(tabUrl);
        loadBrowserTab(m_sessionManager->activeTabId(), tabUrl, true);
        updateHud();
    });

    connect(view, &BrowserView::loadFailed, this, [this, view](const QString &url, int, const QString &errorText) {
        if (view != m_browserView) {
            return;
        }

        if (shouldApplySessionUpdate(url)) {
            clearPendingSessionSwitch();
        }
        m_statusLabel->setText(QStringLiteral("Load failed: %1 (%2)").arg(errorText, url));
    });

    connect(view, &BrowserView::fullscreenModeChanged, this, [this, view](bool fullscreen) {
        if (view != m_browserView) {
            return;
        }

        m_inputManager->setBrowserFullscreenActive(fullscreen);

        if (fullscreen) {
            cancelTemporaryHudHide();
            m_browserController->setHudVisible(false);
            showFullScreen();
        }
    });

    connect(view, &BrowserView::mediaPlaybackChanged, this, [this, view](bool) {
        if (view != m_browserView) {
            return;
        }

        updatePlaybackInhibit();
    });

    connect(view, &BrowserView::virtualKeyboardRequested, this, [this, view](bool requested) {
        qInfo().noquote() << QStringLiteral("BrowserView: virtualKeyboardRequested=%1 active=%2 homeVisible=%3")
                                 .arg(requested ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(view == m_browserView ? QStringLiteral("true") : QStringLiteral("false"))
                                 .arg(m_browserController->homeVisible() ? QStringLiteral("true") : QStringLiteral("false"));
        if (view != m_browserView) {
            return;
        }
        if (!requested) {
            m_inputManager->closeWebKeyboardSession();
            return;
        }
        if (m_browserController->homeVisible() || activeModalDialogWidget()) {
            return;
        }
        view->forceActiveFocus(Qt::OtherFocusReason);
        m_inputManager->openWebKeyboardSession(view);
    });

    connect(view, &BrowserView::renderProcessTerminated, this, [this, view](int status, int errorCode, const QString &errorText) {
        qWarning().noquote()
            << QStringLiteral("Tab renderer terminated: tab=%1 url=%2 status=%3 errorCode=%4 error=%5")
                   .arg(m_activeBrowserTabId.isEmpty() ? QStringLiteral("<none>") : m_activeBrowserTabId,
                        view->currentUrl().isEmpty() ? QStringLiteral("<empty>") : view->currentUrl())
                   .arg(status)
                   .arg(errorCode)
                   .arg(errorText.isEmpty() ? QStringLiteral("<none>") : errorText);

        if (view != m_browserView) {
            return;
        }

        if (shouldApplySessionUpdate(view->currentUrl())) {
            clearPendingSessionSwitch();
        }
        m_statusLabel->setText(QStringLiteral("Renderer crashed (status=%1, code=%2): %3")
                                   .arg(status)
                                   .arg(errorCode)
                                   .arg(errorText.isEmpty() ? QStringLiteral("no additional details") : errorText));
        m_browserController->setHudVisible(true);
        updateHud();
    });

    return view;
}

void MainWindow::applyBrowserReadability(BrowserView *view, const QString &url)
{
    if (!view) {
        return;
    }

    view->setMinimumFontSize(m_siteConfig->minimumFontForUrl(url));
    view->setZoomFactor(m_siteConfig->zoomForUrl(url));
}

void MainWindow::enqueueBrowserViewRemovals(const QStringList &tabIds)
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
    QTimer::singleShot(0, this, &MainWindow::processPendingBrowserViewRemoval);
}

void MainWindow::processPendingBrowserViewRemoval()
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

    QTimer::singleShot(0, this, &MainWindow::processPendingBrowserViewRemoval);
}

void MainWindow::removeBrowserView(const QString &tabId)
{
    BrowserView *view = m_browserViews.take(tabId);
    if (!view) {
        return;
    }

    if (view == m_browserView) {
        m_browserView = nullptr;
        m_activeBrowserTabId.clear();
    }

    const auto finalizeRemoval = [this, view]() {
        if (m_browserStack->indexOf(view) >= 0) {
            m_browserStack->removeWidget(view);
        }
        view->deleteLater();
    };

    if (!view->hasLiveBrowser() && !view->isBrowserClosing()) {
        finalizeRemoval();
        return;
    }

    connect(view, &BrowserView::browserClosed, this, finalizeRemoval, Qt::SingleShotConnection);
    view->closeCurrentPage();
}

void MainWindow::showBrowserTab(const QString &tabId)
{
    BrowserView *view = ensureBrowserView(tabId);
    if (!view) {
        return;
    }

    m_browserView = view;
    m_activeBrowserTabId = tabId;
    m_browserController->showBrowser();
    m_stack->setCurrentWidget(m_browserPage);
    m_browserStack->setCurrentWidget(view);
    m_inputManager->setBrowserFullscreenActive(view->isFullscreenMode());
    syncBrowserControllerFromView(view);
    updatePlaybackInhibit();
    activateWindow();
    if (windowHandle()) {
        windowHandle()->requestActivate();
    }
    view->requestNativeFocus();
    updatePointerMode();
    m_inputManager->refreshSystemCursor();
    QTimer::singleShot(0, this, [this, tabId]() {
        if (m_activeBrowserTabId != tabId || m_browserController->homeVisible() || !m_browserView) {
            return;
        }
        updatePointerMode();
        activateWindow();
        if (windowHandle()) {
            windowHandle()->requestActivate();
        }
        m_browserView->requestNativeFocus();
        m_inputManager->refreshSystemCursor();
    });
    QTimer::singleShot(120, this, [this, tabId]() {
        if (m_activeBrowserTabId != tabId || m_browserController->homeVisible() || !m_browserView) {
            return;
        }
        updatePointerMode();
        activateWindow();
        if (windowHandle()) {
            windowHandle()->requestActivate();
        }
        m_browserView->requestNativeFocus();
        m_inputManager->refreshSystemCursor();
    });
}

void MainWindow::loadBrowserTab(const QString &tabId, const QString &url, bool forceReload)
{
    showBrowserTab(tabId);
    if (!m_browserView) {
        return;
    }

    const QString targetUrl = url.trimmed();
    const bool needsLoad = !targetUrl.isEmpty()
        && (forceReload || m_browserView->currentUrl().trimmed().isEmpty() || m_browserView->currentUrl() != targetUrl);

    if (needsLoad) {
        beginPendingSessionSwitch(targetUrl);
        m_browserController->openUrl(targetUrl);
        return;
    }

    clearPendingSessionSwitch();
    syncBrowserControllerFromView(m_browserView);
    updateHud();
}

void MainWindow::syncBrowserControllerFromView(BrowserView *view)
{
    if (!view) {
        m_browserController->setPageState(QString(), QString(), false, false);
        return;
    }

    m_browserController->setPageState(view->currentUrl(), view->currentTitle(), view->canGoBack(), view->canGoForward());
    if (!view->currentUrl().trimmed().isEmpty()) {
        applyBrowserReadability(view, view->currentUrl());
    }
}

MainWindow::MainWindow(BrowserController *browserController,
                       BookmarkModel *bookmarkModel,
                       HistoryModel *historyModel,
                       InputManager *inputManager,
                       SiteConfigManager *siteConfig,
                       bool startWindowed,
                       const QString &initialUrl,
                       QWidget *parent)
    : QMainWindow(parent)
    , m_browserController(browserController)
    , m_bookmarkModel(bookmarkModel)
    , m_historyModel(historyModel)
    , m_inputManager(inputManager)
    , m_siteConfig(siteConfig)
{
    if (startWindowed) {
        resize(1280, 720);
    } else if (const QScreen *screen = QGuiApplication::primaryScreen()) {
        resize(screen->geometry().size());
    } else {
        resize(1280, 800);
    }
    setWindowTitle(QStringLiteral("AeroCore Browser"));

    auto *central = new QWidget(this);
    auto *rootLayout = new QVBoxLayout(central);
    rootLayout->setContentsMargins(0, 0, 0, 0);

    m_stack = new QStackedWidget(central);
    rootLayout->addWidget(m_stack);
    setCentralWidget(central);

    m_cursorOverlay = new CursorOverlay(this);
    m_cursorOverlay->setGeometry(rect());
    m_pointerIdleTimer = new QTimer(this);
    m_pointerIdleTimer->setSingleShot(true);
    connect(m_pointerIdleTimer, &QTimer::timeout, this, &MainWindow::handlePointerIdleTimeout);
    m_powerInhibitor = new PowerInhibitor(this);

    m_sessionManager = new SessionManager(m_bookmarkModel, this);

    buildHomePage();
    buildBrowserPage();
    buildAddressOverlay();
    buildBookmarkOverlay();
    buildHistoryOverlay();
    buildSettingsOverlay();
    buildExitOverlay();
    connectSignals();
    refreshBookmarkList();
    refreshSessionList();
    applyAppUiScale();
    updateHud();

    m_stack->setCurrentWidget(m_homePage);
    m_bookmarkList->setFocus();
    updatePointerMode();

    if (!initialUrl.isEmpty()) {
        openUrlInNewSession(initialUrl);
    }

    Q_UNUSED(startWindowed);
}

void MainWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab
        && event->modifiers() == Qt::NoModifier
        && !activeModalDialogWidget()
        && !m_browserController->homeVisible()
        && m_browserView) {
        showHudTemporarily(1000);
        event->accept();
        return;
    }

    const QPoint cursorDelta = cursorDeltaForKeyboard(event->key(), event->modifiers());
    if (!cursorDelta.isNull()) {
        m_inputManager->nudgeCursorByKeyboard(cursorDelta);
        event->accept();
        return;
    }

    const int mappedDpadKey = dpadKeyForKeyboard(event->key());
    if (mappedDpadKey != 0 && dispatchNavigationKey(mappedDpadKey)) {
        event->accept();
        return;
    }

    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && activatePrimaryAction()) {
        event->accept();
        return;
    }

    if (dispatchNavigationKey(event->key())) {
        event->accept();
        return;
    }

    const QString action = semanticActionForKey(event->key());
    if (!action.isEmpty()) {
        m_inputManager->triggerAction(action);
        event->accept();
        return;
    }

    QMainWindow::keyPressEvent(event);
}

bool MainWindow::dispatchNavigationKey(int key)
{
    if (navigateActiveDialog(key)) {
        return true;
    }

    if (handleHomeNavigationKey(key)) {
        return true;
    }

    if (!m_browserController->homeVisible() && m_browserView) {
        switch (key) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            m_browserView->sendNavigationKey(key);
            return true;
        default:
            break;
        }
    }

    return false;
}

bool MainWindow::activatePrimaryAction()
{
    if (activeModalDialogWidget() && activateFocusedDialogControl()) {
        return true;
    }

    if (!m_browserController->homeVisible() && m_browserView) {
        m_browserView->sendNavigationKey(Qt::Key_Return);
        return true;
    }

    if (m_browserController->homeVisible()) {
        activateHighlightedHomeItem();
        return true;
    }

    return false;
}

void MainWindow::moveEvent(QMoveEvent *event)
{
    QMainWindow::moveEvent(event);
    updateHud();
    applyPointerCursor();
    updateCustomCursorOverlay(true);
    updateTouchDragIndicator();
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    updateHud();
    applyPointerCursor();
    updateCustomCursorOverlay(true);
    updateTouchDragIndicator();
    updateAddressOverlayGeometry();
    updateBookmarkOverlayGeometry();
    updateHistoryOverlayGeometry();
    updateSettingsOverlayGeometry();
    updateExitOverlayGeometry();
}

int MainWindow::activeArrowCursorSize() const
{
    QScreen *currentScreen = nullptr;
    if (windowHandle()) {
        currentScreen = windowHandle()->screen();
    }
    if (!currentScreen) {
        currentScreen = screen();
    }
    if (!currentScreen && m_browserView && m_browserView->window() && m_browserView->window()->windowHandle()) {
        currentScreen = m_browserView->window()->windowHandle()->screen();
    }

    return adaptiveArrowCursorSizeForScreen(currentScreen);
}

void MainWindow::notePointerActivity()
{
    if (!useCustomCursorRendering()) {
        return;
    }

    if (!m_browserController || m_browserController->homeVisible()) {
        if (m_pointerIdleTimer) {
            m_pointerIdleTimer->stop();
        }
        if (m_pointerIdleHidden) {
            m_pointerIdleHidden = false;
            applyPointerCursor(true);
        }
        return;
    }

    if (m_pointerIdleHidden) {
        m_pointerIdleHidden = false;
        applyPointerCursor(true);
    }

    if (m_pointerIdleTimer) {
        m_pointerIdleTimer->start(kCustomCursorIdleHideMs);
    }
}

void MainWindow::handlePointerIdleTimeout()
{
    if (!useCustomCursorRendering() || !m_browserController || m_browserController->homeVisible()) {
        return;
    }

    if (m_inputManager && m_inputManager->dragLockActive()) {
        m_pointerIdleTimer->start(kCustomCursorIdleHideMs);
        return;
    }

    if (m_pointerIdleHidden) {
        return;
    }

    m_pointerIdleHidden = true;
    applyPointerCursor(true);
}

void MainWindow::updateCustomCursorOverlay(bool forceRaise)
{
    if (!m_cursorOverlay) {
        return;
    }

    auto *overlay = static_cast<CursorOverlay *>(m_cursorOverlay);
    overlay->setGeometry(rect());

    const bool visible = m_browserController
        && !m_browserController->homeVisible()
        && useCustomCursorRendering();
    if (!visible || m_pointerIdleHidden) {
        overlay->updateCursorFrame(QPixmap(), QPoint(), QPoint(), false);
        return;
    }

    const QScreen *currentScreen = windowHandle() ? windowHandle()->screen() : screen();
    const int logicalSize = activeArrowCursorSize();
    const QPixmap pixmap = adaptiveArrowCursorPixmapForScreen(currentScreen);
    const QPoint hotspot = adaptiveArrowCursorHotspotForSize(logicalSize);
    const QPoint globalPos = m_inputManager ? m_inputManager->cursorPosition().toPoint() : QCursor::pos();
    overlay->updateCursorFrame(pixmap, hotspot, globalPos, true);
    if (forceRaise || overlay->isVisible()) {
        overlay->raise();
    }
}

void MainWindow::applyPointerCursor(bool force)
{
    const bool useCustomCursor = useCustomCursorRendering();
    const bool hideCursor = m_browserController->homeVisible() || (useCustomCursor && m_pointerIdleHidden);
    const int arrowCursorSize = (!hideCursor && useCustomCursor) ? activeArrowCursorSize() : -1;
    if (!force && hideCursor == m_pointerCursorHidden && (hideCursor || arrowCursorSize == m_appliedArrowCursorSize)) {
        return;
    }

    const QCursor cursor = (hideCursor || useCustomCursor)
        ? QCursor(Qt::BlankCursor)
        : QCursor(Qt::ArrowCursor);
    if (QApplication::overrideCursor()) {
        QApplication::changeOverrideCursor(cursor);
    } else {
        QApplication::setOverrideCursor(cursor);
    }
    setCursor(cursor);
    if (centralWidget()) {
        centralWidget()->setCursor(cursor);
    }
    if (m_stack) {
        m_stack->setCursor(cursor);
    }
    if (m_homePage) {
        m_homePage->setCursor(cursor);
    }
    if (m_browserPage) {
        m_browserPage->setCursor(cursor);
    }
    if (m_browserStack) {
        m_browserStack->setCursor(cursor);
    }
    if (m_hud) {
        m_hud->setCursor(cursor);
    }
    if (m_addressOverlay) {
        m_addressOverlay->setCursor(cursor);
    }
    if (m_addressPanel) {
        m_addressPanel->setCursor(cursor);
    }
    if (m_bookmarkOverlay) {
        m_bookmarkOverlay->setCursor(cursor);
    }
    if (m_bookmarkPanel) {
        m_bookmarkPanel->setCursor(cursor);
    }
    if (m_historyOverlay) {
        m_historyOverlay->setCursor(cursor);
    }
    if (m_historyPanel) {
        m_historyPanel->setCursor(cursor);
    }
    if (m_settingsOverlay) {
        m_settingsOverlay->setCursor(cursor);
    }
    if (m_settingsPanel) {
        m_settingsPanel->setCursor(cursor);
    }
    if (m_exitOverlay) {
        m_exitOverlay->setCursor(cursor);
    }
    if (m_exitPanel) {
        m_exitPanel->setCursor(cursor);
    }
    if (m_browserView) {
        m_browserView->setCursor(cursor);
        m_browserView->setNativeCursorVisible(!hideCursor && !useCustomCursor && !m_inputManager->dragLockActive());
    }

    m_pointerCursorHidden = hideCursor;
    m_appliedArrowCursorSize = arrowCursorSize;
    updateCustomCursorOverlay(true);
}

void MainWindow::buildHomePage()
{
    m_homePage = new QWidget(m_stack);
    auto *layout = new QVBoxLayout(m_homePage);
    layout->setContentsMargins(40, 32, 40, 32);
    layout->setSpacing(24);

    auto *title = new QLabel(QStringLiteral("AeroCore"), m_homePage);
    title->setProperty("role", "title");
    auto *subtitle = new QLabel(QStringLiteral("SteamOS big-screen browser MVP powered by CEF"), m_homePage);
    subtitle->setProperty("role", "subtitleMuted");
    m_homeDebugLabel = new QLabel(m_homePage);
    m_homeDebugLabel->setProperty("role", "muted");
    m_homeDebugLabel->setWordWrap(true);

    auto *buttonRow = new QHBoxLayout();
    m_homeAddressButton = new QPushButton(QStringLiteral("Address"), m_homePage);
    m_homeHistoryButton = new QPushButton(QStringLiteral("History"), m_homePage);
    m_homeSettingsButton = new QPushButton(QStringLiteral("Settings"), m_homePage);
    m_homeAddressButton->setProperty("homeTopButton", true);
    m_homeHistoryButton->setProperty("homeTopButton", true);
    m_homeSettingsButton->setProperty("homeTopButton", true);
    buttonRow->addWidget(m_homeAddressButton);
    buttonRow->addWidget(m_homeHistoryButton);
    buttonRow->addWidget(m_homeSettingsButton);
    buttonRow->addStretch(1);

    auto *listsRow = new QHBoxLayout();
    m_bookmarkList = new QListWidget(m_homePage);
    m_recentList = new QListWidget(m_homePage);
    m_bookmarkList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_recentList->setSelectionMode(QAbstractItemView::SingleSelection);
    m_bookmarkList->setAlternatingRowColors(false);
    m_recentList->setAlternatingRowColors(false);
    listsRow->addWidget(m_bookmarkList, 1);
    listsRow->addWidget(m_recentList, 1);

    layout->addWidget(title);
    layout->addWidget(subtitle);
    layout->addWidget(m_homeDebugLabel);
    layout->addLayout(buttonRow);
    layout->addLayout(listsRow, 1);

    m_stack->addWidget(m_homePage);

    connect(m_homeAddressButton, &QPushButton::clicked, this, &MainWindow::openAddressDialog);
    connect(m_homeHistoryButton, &QPushButton::clicked, this, &MainWindow::openHistoryDialog);
    connect(m_homeSettingsButton, &QPushButton::clicked, this, &MainWindow::openSettingsDialog);
    connect(m_bookmarkList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        openSite(item->data(Qt::UserRole).toString());
    });
    connect(m_bookmarkList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openSite(item->data(Qt::UserRole).toString());
    });
    connect(m_bookmarkList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row >= 0) {
            m_bookmarkCursor = row;
        }
        refreshSessionList();
    });
    connect(m_recentList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        openSession(item->data(Qt::UserRole).toString());
    });
    connect(m_recentList, &QListWidget::itemClicked, this, [this](QListWidgetItem *item) {
        openSession(item->data(Qt::UserRole).toString());
    });
}

void MainWindow::buildBrowserPage()
{
    m_browserPage = new QWidget(m_stack);
    auto *stackLayout = new QStackedLayout(m_browserPage);
    stackLayout->setStackingMode(QStackedLayout::StackAll);
    stackLayout->setContentsMargins(0, 0, 0, 0);
    stackLayout->setSpacing(0);

    m_browserStack = new QStackedWidget(m_browserPage);
    stackLayout->addWidget(m_browserStack);

    m_hud = new QWidget(m_browserPage);
    m_hud->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_hud->setStyleSheet(QStringLiteral("background: rgba(7, 11, 16, 180);"));

    m_touchDragIndicator = new QWidget(m_browserPage);
    m_touchDragIndicator->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_touchDragIndicator->hide();
    m_touchDragIndicator->setFixedSize(scaledPx(18), scaledPx(18));
    m_touchDragIndicator->setStyleSheet(QStringLiteral(
        "background: rgba(127, 192, 255, 180);"
        "border: 2px solid rgba(255, 255, 255, 220);"
        "border-radius: 9px;"));

    auto *hudLayout = new QVBoxLayout(m_hud);
    hudLayout->setContentsMargins(24, 24, 24, 24);
    hudLayout->setSpacing(8);
    m_titleLabel = new QLabel(m_hud);
    m_titleLabel->setProperty("role", "hudTitle");
    m_titleLabel->setWordWrap(true);
    m_urlLabel = new QLabel(m_hud);
    m_urlLabel->setProperty("role", "muted");
    m_urlLabel->setWordWrap(true);
    m_modeLabel = new QLabel(m_hud);
    m_modeLabel->setProperty("role", "muted");
    m_modeLabel->setWordWrap(true);
    m_statusLabel = new QLabel(m_hud);
    m_statusLabel->setProperty("role", "muted");
    m_statusLabel->setWordWrap(true);
    auto *hudListsRow = new QHBoxLayout();
    m_tabListLabel = new QLabel(m_hud);
    m_tabListLabel->setProperty("role", "muted");
    m_tabListLabel->setWordWrap(false);
    m_tabListLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    m_sessionListLabel = new QLabel(m_hud);
    m_sessionListLabel->setProperty("role", "muted");
    m_sessionListLabel->setWordWrap(false);
    m_sessionListLabel->setAlignment(Qt::AlignLeft | Qt::AlignTop);
    hudListsRow->addWidget(m_sessionListLabel, 1);
    hudListsRow->addWidget(m_tabListLabel, 1);
    hudLayout->addWidget(m_titleLabel);
    hudLayout->addWidget(m_urlLabel);
    hudLayout->addWidget(m_modeLabel);
    hudLayout->addWidget(m_statusLabel);
    hudLayout->addLayout(hudListsRow);

    m_stack->addWidget(m_browserPage);
}

void MainWindow::buildAddressOverlay()
{
    QWidget *host = centralWidget();
    if (!host) {
        return;
    }

    m_addressOverlay = new QWidget(host);
    m_addressOverlay->setProperty("dialogLikeOverlay", true);
    m_addressOverlay->hide();

    auto *overlayLayout = new QVBoxLayout(m_addressOverlay);
    overlayLayout->setContentsMargins(scaledPx(96), scaledPx(64), scaledPx(96), scaledPx(64));
    overlayLayout->setAlignment(Qt::AlignCenter);

    m_addressPanel = new QWidget(m_addressOverlay);
    auto *panelLayout = new QVBoxLayout(m_addressPanel);
    panelLayout->setContentsMargins(scaledPx(28), scaledPx(24), scaledPx(28), scaledPx(24));
    panelLayout->setSpacing(scaledPx(12));

    auto *titleLabel = new QLabel(QStringLiteral("Open address or search"), m_addressPanel);
    titleLabel->setProperty("role", "hudTitle");
    m_addressLineEdit = new QLineEdit(m_addressPanel);
    m_addressOpenButton = new QPushButton(QStringLiteral("Open"), m_addressPanel);
    m_addressCancelButton = new QPushButton(QStringLiteral("Cancel"), m_addressPanel);

    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(m_addressLineEdit);
    panelLayout->addWidget(m_addressOpenButton);
    panelLayout->addWidget(m_addressCancelButton);
    overlayLayout->addWidget(m_addressPanel, 0, Qt::AlignCenter);

    auto openEnteredAddress = [this]() {
        if (!m_addressLineEdit) {
            return;
        }

        const QString text = m_addressLineEdit->text().trimmed();
        if (text.isEmpty()) {
            return;
        }

        if (m_addressOverlay) {
            m_addressOverlay->hide();
        }
        openUrlInNewSession(text, text);
    };

    connect(m_addressOpenButton, &QPushButton::clicked, this, openEnteredAddress);
    connect(m_addressLineEdit, &QLineEdit::returnPressed, this, openEnteredAddress);
    connect(m_addressCancelButton, &QPushButton::clicked, this, &MainWindow::closeAddressOverlay);

    updateAddressOverlayGeometry();
}

void MainWindow::buildBookmarkOverlay()
{
    QWidget *host = centralWidget();
    if (!host) {
        return;
    }

    m_bookmarkOverlay = new QWidget(host);
    m_bookmarkOverlay->setProperty("dialogLikeOverlay", true);
    m_bookmarkOverlay->hide();

    auto *overlayLayout = new QVBoxLayout(m_bookmarkOverlay);
    overlayLayout->setContentsMargins(scaledPx(72), scaledPx(48), scaledPx(72), scaledPx(48));
    overlayLayout->setAlignment(Qt::AlignCenter);

    m_bookmarkPanel = new QWidget(m_bookmarkOverlay);
    auto *panelLayout = new QVBoxLayout(m_bookmarkPanel);
    panelLayout->setContentsMargins(scaledPx(28), scaledPx(24), scaledPx(28), scaledPx(24));
    panelLayout->setSpacing(scaledPx(12));

    auto *titleLabel = new QLabel(QStringLiteral("Bookmarks"), m_bookmarkPanel);
    titleLabel->setProperty("role", "hudTitle");
    m_bookmarkDialogList = new QListWidget(m_bookmarkPanel);
    m_bookmarkToggleButton = new QPushButton(QStringLiteral("Bookmark / Unbookmark Current Page"), m_bookmarkPanel);
    m_bookmarkCloseButton = new QPushButton(QStringLiteral("Close"), m_bookmarkPanel);

    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(m_bookmarkDialogList, 1);
    panelLayout->addWidget(m_bookmarkToggleButton);
    panelLayout->addWidget(m_bookmarkCloseButton);
    overlayLayout->addWidget(m_bookmarkPanel, 0, Qt::AlignCenter);

    connect(m_bookmarkDialogList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const QString url = item ? item->data(Qt::UserRole).toString() : QString();
        if (url.isEmpty()) {
            return;
        }

        if (m_bookmarkOverlay) {
            m_bookmarkOverlay->hide();
        }
        openSite(url);
    });
    connect(m_bookmarkToggleButton, &QPushButton::clicked, this, [this]() {
        const QString url = m_browserController->currentUrl();
        if (url.isEmpty()) {
            return;
        }

        m_bookmarkModel->toggleBookmark(m_browserController->currentTitle(), url);
        refreshBookmarkList();
        refreshBookmarkOverlayList();

        if (m_bookmarkDialogList && m_bookmarkDialogList->count() > 0) {
            m_bookmarkDialogList->setFocus();
        } else if (m_bookmarkCloseButton) {
            m_bookmarkCloseButton->setFocus();
        }
    });
    connect(m_bookmarkCloseButton, &QPushButton::clicked, this, &MainWindow::closeBookmarkOverlay);

    updateBookmarkOverlayGeometry();
}

void MainWindow::buildHistoryOverlay()
{
    QWidget *host = centralWidget();
    if (!host) {
        return;
    }

    m_historyOverlay = new QWidget(host);
    m_historyOverlay->setProperty("dialogLikeOverlay", true);
    m_historyOverlay->hide();

    auto *overlayLayout = new QVBoxLayout(m_historyOverlay);
    overlayLayout->setContentsMargins(scaledPx(72), scaledPx(48), scaledPx(72), scaledPx(48));
    overlayLayout->setAlignment(Qt::AlignCenter);

    m_historyPanel = new QWidget(m_historyOverlay);
    auto *panelLayout = new QVBoxLayout(m_historyPanel);
    panelLayout->setContentsMargins(scaledPx(28), scaledPx(24), scaledPx(28), scaledPx(24));
    panelLayout->setSpacing(scaledPx(12));

    auto *titleLabel = new QLabel(QStringLiteral("Recent history"), m_historyPanel);
    titleLabel->setProperty("role", "hudTitle");
    m_historyDialogList = new QListWidget(m_historyPanel);
    m_historyDialogList->setProperty("historyDialogList", true);
    m_historyCloseButton = new QPushButton(QStringLiteral("Close"), m_historyPanel);

    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(m_historyDialogList, 1);
    panelLayout->addWidget(m_historyCloseButton);
    overlayLayout->addWidget(m_historyPanel, 0, Qt::AlignCenter);

    connect(m_historyDialogList, &QListWidget::itemActivated, this, [this](QListWidgetItem *item) {
        const QString url = item ? item->data(Qt::UserRole).toString() : QString();
        if (url.isEmpty()) {
            return;
        }

        if (m_historyOverlay) {
            m_historyOverlay->hide();
        }
        openUrlInNewSession(url);
    });
    connect(m_historyCloseButton, &QPushButton::clicked, this, &MainWindow::closeHistoryOverlay);

    updateHistoryOverlayGeometry();
}

void MainWindow::buildSettingsOverlay()
{
    QWidget *host = centralWidget();
    if (!host) {
        return;
    }

    m_settingsOverlay = new QWidget(host);
    m_settingsOverlay->setProperty("dialogLikeOverlay", true);
    m_settingsOverlay->hide();

    auto *overlayLayout = new QVBoxLayout(m_settingsOverlay);
    overlayLayout->setContentsMargins(scaledPx(96), scaledPx(64), scaledPx(96), scaledPx(64));
    overlayLayout->setAlignment(Qt::AlignCenter);

    m_settingsPanel = new QWidget(m_settingsOverlay);
    auto *panelLayout = new QVBoxLayout(m_settingsPanel);
    panelLayout->setContentsMargins(scaledPx(28), scaledPx(24), scaledPx(28), scaledPx(24));
    panelLayout->setSpacing(scaledPx(12));

    auto *titleLabel = new QLabel(QStringLiteral("10-foot readability model"), m_settingsPanel);
    titleLabel->setProperty("role", "hudTitle");
    m_settingsSystemLabel = new QLabel(m_settingsPanel);
    m_settingsSystemLabel->setProperty("role", "muted");

    auto *appUiDownButton = new QPushButton(QStringLiteral("App UI -"), m_settingsPanel);
    auto *appUiUpButton = new QPushButton(QStringLiteral("App UI +"), m_settingsPanel);
    auto *appUiAutoButton = new QPushButton(QStringLiteral("Auto App UI"), m_settingsPanel);
    auto *zoomInButton = new QPushButton(QStringLiteral("Page Zoom In"), m_settingsPanel);
    auto *zoomOutButton = new QPushButton(QStringLiteral("Page Zoom Out"), m_settingsPanel);
    auto *pageAutoButton = new QPushButton(QStringLiteral("Auto Page Zoom"), m_settingsPanel);
    auto *goHomeButton = new QPushButton(QStringLiteral("Return Home"), m_settingsPanel);
    auto *closeButton = new QPushButton(QStringLiteral("Close"), m_settingsPanel);
    m_settingsNavigationWidgets = {appUiDownButton,
                                   appUiUpButton,
                                   appUiAutoButton,
                                   zoomInButton,
                                   zoomOutButton,
                                   pageAutoButton,
                                   goHomeButton,
                                   closeButton};
    m_settingsDefaultButton = zoomInButton;

    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(m_settingsSystemLabel);
    panelLayout->addWidget(appUiDownButton);
    panelLayout->addWidget(appUiUpButton);
    panelLayout->addWidget(appUiAutoButton);
    panelLayout->addWidget(zoomInButton);
    panelLayout->addWidget(zoomOutButton);
    panelLayout->addWidget(pageAutoButton);
    panelLayout->addWidget(goHomeButton);
    panelLayout->addWidget(closeButton);
    overlayLayout->addWidget(m_settingsPanel, 0, Qt::AlignCenter);

    connect(appUiDownButton, &QPushButton::clicked, this, [this]() {
        m_siteConfig->setAppUiScale(std::max(1.0, m_siteConfig->appUiScale() - 0.1));
    });
    connect(appUiUpButton, &QPushButton::clicked, this, [this]() {
        m_siteConfig->setAppUiScale(std::min(2.4, m_siteConfig->appUiScale() + 0.1));
    });
    connect(appUiAutoButton, &QPushButton::clicked, this, [this]() {
        m_siteConfig->resetAppUiScale();
    });
    connect(zoomInButton, &QPushButton::clicked, this, [this]() {
        const double nextZoom = std::min(3.0, m_siteConfig->zoomForUrl(m_browserController->currentUrl()) + 0.1);
        m_siteConfig->setZoomForUrl(m_browserController->currentUrl(), nextZoom);
        if (m_browserView) {
            m_browserView->setZoomFactor(nextZoom);
        }
        updateHud();
    });
    connect(zoomOutButton, &QPushButton::clicked, this, [this]() {
        const double nextZoom = std::max(0.8, m_siteConfig->zoomForUrl(m_browserController->currentUrl()) - 0.1);
        m_siteConfig->setZoomForUrl(m_browserController->currentUrl(), nextZoom);
        if (m_browserView) {
            m_browserView->setZoomFactor(nextZoom);
        }
        updateHud();
    });
    connect(pageAutoButton, &QPushButton::clicked, this, [this]() {
        m_siteConfig->resetSite(m_browserController->currentUrl());
        if (m_browserView) {
            applyBrowserReadability(m_browserView, m_browserController->currentUrl());
        }
        updateHud();
    });
    connect(goHomeButton, &QPushButton::clicked, this, [this]() {
        m_browserController->goHome();
        closeSettingsOverlay();
    });
    connect(closeButton, &QPushButton::clicked, this, &MainWindow::closeSettingsOverlay);

    updateSettingsOverlayGeometry();
}

void MainWindow::buildExitOverlay()
{
    QWidget *host = centralWidget();
    if (!host) {
        return;
    }

    m_exitOverlay = new QWidget(host);
    m_exitOverlay->setProperty("dialogLikeOverlay", true);
    m_exitOverlay->hide();

    auto *overlayLayout = new QVBoxLayout(m_exitOverlay);
    overlayLayout->setContentsMargins(scaledPx(96), scaledPx(64), scaledPx(96), scaledPx(64));
    overlayLayout->setAlignment(Qt::AlignCenter);

    m_exitPanel = new QWidget(m_exitOverlay);
    auto *panelLayout = new QVBoxLayout(m_exitPanel);
    panelLayout->setContentsMargins(scaledPx(28), scaledPx(24), scaledPx(28), scaledPx(24));
    panelLayout->setSpacing(scaledPx(12));

    auto *titleLabel = new QLabel(QStringLiteral("Exit AeroCore"), m_exitPanel);
    titleLabel->setProperty("role", "hudTitle");
    m_exitMessageLabel = new QLabel(QStringLiteral("是否确定退出 AeroCore Browser？"), m_exitPanel);
    m_exitMessageLabel->setWordWrap(true);
    m_exitConfirmButton = new QPushButton(QStringLiteral("Exit"), m_exitPanel);
    m_exitCancelButton = new QPushButton(QStringLiteral("Cancel"), m_exitPanel);

    panelLayout->addWidget(titleLabel);
    panelLayout->addWidget(m_exitMessageLabel);
    panelLayout->addWidget(m_exitConfirmButton);
    panelLayout->addWidget(m_exitCancelButton);
    overlayLayout->addWidget(m_exitPanel, 0, Qt::AlignCenter);

    connect(m_exitConfirmButton, &QPushButton::clicked, this, []() {
        qApp->quit();
    });
    connect(m_exitCancelButton, &QPushButton::clicked, this, &MainWindow::closeExitOverlay);

    updateExitOverlayGeometry();
}

void MainWindow::connectSignals()
{
    connect(qApp, &QApplication::focusChanged, this, [this](QWidget *, QWidget *now) {
        if (!m_browserController->homeVisible()) {
            return;
        }

        if (activeModalDialogWidget()) {
            return;
        }

        if (m_pendingHomeOverlayReturnButton) {
            return;
        }

        if (now && (now == m_homePage || m_homePage->isAncestorOf(now))) {
            updateHomeSectionVisualState();
            return;
        }

        QTimer::singleShot(0, this, [this]() {
            if (m_browserController->homeVisible()) {
                restoreHomeFocus();
            }
        });
    });

    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        updatePlaybackInhibit();
        if (state != Qt::ApplicationActive) {
            return;
        }

        updatePointerMode();
        if (m_browserController->homeVisible()) {
            restoreHomeFocus();
        } else if (m_browserView) {
            m_browserView->setFocus();
            m_inputManager->refreshSystemCursor();
            QTimer::singleShot(80, this, [this]() {
                if (m_browserController->homeVisible() || !m_browserView) {
                    return;
                }
                updatePointerMode();
                m_browserView->setFocus();
                m_inputManager->refreshSystemCursor();
            });
        }
    });

    connect(m_siteConfig, &SiteConfigManager::appUiScaleChanged, this, [this](double) {
        applyAppUiScale();
    });

    connect(m_siteConfig, &SiteConfigManager::defaultsChanged, this, [this]() {
        if (m_browserView && !m_browserController->homeVisible()) {
            applyBrowserReadability(m_browserView, m_browserController->currentUrl());
        }
        updateHud();
    });

    connect(m_siteConfig, &SiteConfigManager::siteConfigChanged, this, [this](const QString &) {
        if (m_browserView && !m_browserController->homeVisible()) {
            applyBrowserReadability(m_browserView, m_browserController->currentUrl());
        }
        updateHud();
    });

    connect(m_browserController, &BrowserController::navigateRequested, this, [this](const QString &url) {
        if (!m_browserView) {
            const QString tabId = m_sessionManager->activeTabId();
            if (tabId.isEmpty()) {
                return;
            }
            showBrowserTab(tabId);
        }

        m_stack->setCurrentWidget(m_browserPage);
        m_browserView->loadUrl(url);
        m_browserView->setFocus();
    });

    connect(m_browserController, &BrowserController::homeVisibleChanged, this, [this]() {
        m_stack->setCurrentWidget(m_browserController->homeVisible() ? m_homePage : m_browserPage);
        updatePlaybackInhibit();
        updatePointerMode();
        if (m_browserController->homeVisible()) {
            clearPendingSessionSwitch();
            restoreHomeFocus();
        } else if (m_browserView) {
            m_browserView->setFocus();
            m_inputManager->refreshSystemCursor();
        }
        updateHud();
    });

    connect(m_browserController, &BrowserController::hudVisibleChanged, this, [this]() {
        updateHud();
    });

    connect(m_browserController, &BrowserController::homeVisibleChanged, this, [this]() {
        if (m_browserController->homeVisible() || !m_browserView) {
            m_inputManager->setBrowserFullscreenActive(false);
            if (m_pointerIdleTimer) {
                m_pointerIdleTimer->stop();
            }
            m_pointerIdleHidden = false;
            updateCustomCursorOverlay(true);
            return;
        }

        m_inputManager->setBrowserFullscreenActive(m_browserView->isFullscreenMode());
        notePointerActivity();
        updateCustomCursorOverlay(true);
    });

    connect(m_inputManager, &InputManager::actionTriggered, this, &MainWindow::handleAction);
    connect(m_inputManager, &InputManager::pointerActivity, this, &MainWindow::notePointerActivity);
    connect(m_inputManager, &InputManager::cursorPositionChanged, this, [this]() {
        updateCustomCursorOverlay();
        updateTouchDragIndicator();
    });
    connect(m_inputManager, &InputManager::browserCursorSyncRequested, this, [this]() {
        if (!m_browserController->homeVisible() && m_browserView) {
            applyPointerCursor(true);
            if (!m_inputManager->dragLockActive()) {
                m_browserView->syncPointerFromGlobal(m_inputManager->cursorPosition().toPoint(),
                                                     m_inputManager->heldMouseButtons());
            }
            updateCustomCursorOverlay(true);
            updateTouchDragIndicator();
        }
    });
    connect(m_inputManager, &InputManager::browserMouseButtonRequested, this, [this](const QPoint &globalPos, Qt::MouseButton button, bool pressed, Qt::MouseButtons buttons) {
        if (m_browserController->homeVisible() || !m_browserView) {
            return;
        }

        const QPoint local = m_browserView->mapFromGlobal(globalPos);
        QMouseEvent event(
            pressed ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
            QPointF(local),
            QPointF(m_browserView->window()->mapFromGlobal(globalPos)),
            QPointF(globalPos),
            button,
            buttons,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(m_browserView, &event);
    });
    connect(m_inputManager, &InputManager::browserTouchRequested, this, [this](const QPoint &globalPos, InputManager::BrowserTouchEventType eventType) {
        if (m_browserController->homeVisible() || !m_browserView) {
            return;
        }

        switch (eventType) {
        case InputManager::BrowserTouchEventType::Pressed:
            m_browserView->beginTouchDragFromGlobal(globalPos);
            break;
        case InputManager::BrowserTouchEventType::Moved:
            m_browserView->updateTouchDragFromGlobal(globalPos);
            break;
        case InputManager::BrowserTouchEventType::Released:
            m_browserView->endTouchDragFromGlobal(globalPos);
            break;
        }
    });
    connect(m_inputManager, &InputManager::browserClickRequested, this, [this]() {
        if (activeModalDialogWidget() && activateFocusedDialogControl()) {
            return;
        }
        if (!m_browserController->homeVisible() && m_browserView) {
            m_browserView->sendNavigationKey(Qt::Key_Return);
        } else if (m_inputManager->selectionInputSource() == InputManager::SelectionInputSource::Dpad) {
            activateHighlightedHomeItem();
        }
    });
    connect(m_inputManager, &InputManager::browserNavigationKeyRequested, this, [this](int key) {
        if (navigateActiveDialog(key)) {
            return;
        }
        if (handleHomeNavigationKey(key)) {
            return;
        }
        if (!m_browserController->homeVisible() && m_browserView) {
            m_browserView->sendNavigationKey(key);
        }
    });

    connect(m_bookmarkModel, &BookmarkModel::countChanged, this, &MainWindow::refreshBookmarkList);
    connect(m_bookmarkModel, &BookmarkModel::countChanged, this, &MainWindow::refreshBookmarkOverlayList);
    connect(m_historyModel, &HistoryModel::countChanged, this, &MainWindow::refreshHistoryOverlayList);
    connect(m_sessionManager, &SessionManager::sessionsChanged, this, [this]() {
        refreshSessionList();
        updateHud();
    });
}

void MainWindow::refreshBookmarkList()
{
    m_bookmarkList->clear();
    for (int row = 0; row < m_bookmarkModel->rowCount(); ++row) {
        const QModelIndex index = m_bookmarkModel->index(row, 0);
        auto *item = makeListItem(index.data(BookmarkModel::TitleRole).toString(), index.data(BookmarkModel::UrlRole).toString());
        item->setData(Qt::UserRole, index.data(BookmarkModel::UrlRole));
        m_bookmarkList->addItem(item);
    }
    if (m_bookmarkList->count() > 0) {
        m_bookmarkCursor = qBound(0, m_bookmarkCursor, m_bookmarkList->count() - 1);
        m_bookmarkList->setCurrentRow(m_bookmarkCursor);
    }
}

void MainWindow::refreshSessionList()
{
    m_recentList->clear();
    const QString siteUrl = selectedBookmarkUrl();
    const QString siteId = m_sessionManager->siteIdForUrl(siteUrl);
    const QList<BrowserSession> sessions = m_sessionManager->sessionsForSite(siteId);
    for (const BrowserSession &session : sessions) {
        const QString detail = QStringLiteral("%1  %2")
                                   .arg(session.updatedAt.toLocalTime().toString(QStringLiteral("MM-dd hh:mm")),
                                        session.currentUrl);
        auto *item = makeListItem(session.title, detail);
        item->setData(Qt::UserRole, session.sessionId);
        m_recentList->addItem(item);
    }

    if (m_recentList->count() == 0) {
        return;
    }

    int activeRow = -1;
    for (int row = 0; row < sessions.size(); ++row) {
        if (sessions.at(row).sessionId == m_sessionManager->activeSessionId()) {
            activeRow = row;
            break;
        }
    }

    m_recentList->setCurrentRow(activeRow >= 0 ? activeRow : 0);
}

void MainWindow::openSite(const QString &url)
{
    syncBookmarkCursor(url);
    const QString resumeUrl = m_sessionManager->activateOrCreateSiteSession(url);
    if (!resumeUrl.isEmpty()) {
        loadBrowserTab(m_sessionManager->activeTabId(), resumeUrl, false);
    }
}

void MainWindow::openAddressDialog()
{
    if (!m_addressOverlay) {
        buildAddressOverlay();
    }
    if (!m_addressOverlay || !m_addressLineEdit) {
        return;
    }

    m_pendingHomeOverlayReturnButton = currentHomeTopButton();
    rememberHomeTopButtonFocus(m_pendingHomeOverlayReturnButton);

    m_addressLineEdit->setText(m_browserController->currentUrl());
    m_addressLineEdit->selectAll();
    updateAddressOverlayGeometry();
    m_addressOverlay->show();
    m_addressOverlay->raise();
    m_addressPanel->raise();
    m_addressLineEdit->setFocus();
}

void MainWindow::openBookmarkDialog()
{
    if (!m_bookmarkOverlay) {
        buildBookmarkOverlay();
    }
    if (!m_bookmarkOverlay) {
        return;
    }

    refreshBookmarkOverlayList();
    updateBookmarkOverlayGeometry();
    m_bookmarkOverlay->show();
    m_bookmarkOverlay->raise();
    m_bookmarkPanel->raise();

    if (m_bookmarkDialogList && m_bookmarkDialogList->count() > 0) {
        m_bookmarkDialogList->setFocus();
    } else if (m_bookmarkToggleButton) {
        m_bookmarkToggleButton->setFocus();
    }
}

void MainWindow::openHistoryDialog()
{
    if (!m_historyOverlay) {
        buildHistoryOverlay();
    }
    if (!m_historyOverlay) {
        return;
    }

    m_pendingHomeOverlayReturnButton = currentHomeTopButton();
    rememberHomeTopButtonFocus(m_pendingHomeOverlayReturnButton);

    refreshHistoryOverlayList();
    updateHistoryOverlayGeometry();
    m_historyOverlay->show();
    m_historyOverlay->raise();
    m_historyPanel->raise();

    if (m_historyDialogList && m_historyDialogList->count() > 0) {
        m_historyDialogList->setFocus();
    } else if (m_historyCloseButton) {
        m_historyCloseButton->setFocus();
    }
}

void MainWindow::openSettingsDialog()
{
    if (!m_settingsOverlay) {
        buildSettingsOverlay();
    }
    if (!m_settingsOverlay) {
        return;
    }

    m_pendingHomeOverlayReturnButton = currentHomeTopButton();
    rememberHomeTopButtonFocus(m_pendingHomeOverlayReturnButton);

    const UiScaleReading systemUiScale = detectSystemUiScale(this);
    m_settingsSystemLabel->setText(
        QStringLiteral("System UI scale: %1x (%2)\n%3")
            .arg(QString::number(systemUiScale.value, 'f', 2),
                 systemUiScale.source,
                 m_siteConfig->automaticReadabilitySummary()));
    updateSettingsOverlayGeometry();
    m_settingsOverlay->show();
    m_settingsOverlay->raise();
    m_settingsPanel->raise();
    if (m_settingsDefaultButton) {
        m_settingsDefaultButton->setFocus();
    }
}

void MainWindow::closeSettingsOverlay()
{
    if (!m_settingsOverlay || !m_settingsOverlay->isVisible()) {
        return;
    }

    m_settingsOverlay->hide();
    restoreFocusAfterOverlayClose();
}

void MainWindow::closeAddressOverlay()
{
    if (!m_addressOverlay || !m_addressOverlay->isVisible()) {
        return;
    }

    m_addressOverlay->hide();
    restoreFocusAfterOverlayClose();
}

void MainWindow::refreshBookmarkOverlayList()
{
    if (!m_bookmarkDialogList) {
        return;
    }

    m_bookmarkDialogList->clear();
    for (int row = 0; row < m_bookmarkModel->rowCount(); ++row) {
        const QModelIndex index = m_bookmarkModel->index(row, 0);
        auto *item = makeListItem(index.data(BookmarkModel::TitleRole).toString(), index.data(BookmarkModel::UrlRole).toString());
        item->setData(Qt::UserRole, index.data(BookmarkModel::UrlRole));
        m_bookmarkDialogList->addItem(item);
    }

    if (m_bookmarkDialogList->count() > 0) {
        m_bookmarkDialogList->setCurrentRow(qBound(0, m_bookmarkCursor, m_bookmarkDialogList->count() - 1));
    }
}

void MainWindow::closeBookmarkOverlay()
{
    if (!m_bookmarkOverlay || !m_bookmarkOverlay->isVisible()) {
        return;
    }

    m_bookmarkOverlay->hide();
    restoreFocusAfterOverlayClose();
}

void MainWindow::refreshHistoryOverlayList()
{
    if (!m_historyDialogList) {
        return;
    }

    m_historyDialogList->clear();
    for (int row = 0; row < m_historyModel->rowCount(); ++row) {
        const QModelIndex index = m_historyModel->index(row, 0);
        auto *item = makeListItem(index.data(HistoryModel::TitleRole).toString(), index.data(HistoryModel::UrlRole).toString());
        item->setData(Qt::UserRole, index.data(HistoryModel::UrlRole));
        m_historyDialogList->addItem(item);
    }

    if (m_historyDialogList->count() > 0) {
        m_historyDialogList->setCurrentRow(0);
    }
}

void MainWindow::closeHistoryOverlay()
{
    if (!m_historyOverlay || !m_historyOverlay->isVisible()) {
        return;
    }

    m_historyOverlay->hide();
    restoreFocusAfterOverlayClose();
}

void MainWindow::closeExitOverlay()
{
    if (!m_exitOverlay || !m_exitOverlay->isVisible()) {
        return;
    }

    m_exitOverlay->hide();
    restoreFocusAfterOverlayClose();
}

void MainWindow::restoreFocusAfterOverlayClose()
{
    if (m_browserController->homeVisible()) {
        QPushButton *targetButton = m_pendingHomeOverlayReturnButton;
        QTimer::singleShot(0, this, [this, targetButton]() {
            if (!m_browserController->homeVisible() || activeModalDialogWidget()) {
                return;
            }

            if (m_pendingHomeOverlayReturnButton == targetButton) {
                m_pendingHomeOverlayReturnButton = nullptr;
            }

            if (targetButton && targetButton->isVisible() && targetButton->isEnabled()) {
                targetButton->setFocus();
                updateHomeSectionVisualState();
                return;
            }

            restoreHomeFocus();
        });
        return;
    }

    m_pendingHomeOverlayReturnButton = nullptr;

    if (m_browserView) {
        m_browserView->setFocus();
        m_inputManager->refreshSystemCursor();
    }
}

void MainWindow::updateSettingsOverlayGeometry()
{
    if (!m_settingsOverlay || !centralWidget()) {
        return;
    }

    m_settingsOverlay->setGeometry(centralWidget()->rect());
    if (m_settingsPanel) {
        m_settingsPanel->setFixedWidth(qMin(scaledPx(560), qMax(scaledPx(360), centralWidget()->width() - scaledPx(96))));
    }
}

void MainWindow::updateAddressOverlayGeometry()
{
    if (!m_addressOverlay || !centralWidget()) {
        return;
    }

    m_addressOverlay->setGeometry(centralWidget()->rect());
    if (m_addressPanel) {
        m_addressPanel->setFixedWidth(qMin(scaledPx(720), qMax(scaledPx(420), centralWidget()->width() - scaledPx(96))));
    }
}

void MainWindow::updateBookmarkOverlayGeometry()
{
    if (!m_bookmarkOverlay || !centralWidget()) {
        return;
    }

    m_bookmarkOverlay->setGeometry(centralWidget()->rect());
    if (m_bookmarkPanel) {
        m_bookmarkPanel->setFixedWidth(qMin(scaledPx(820), qMax(scaledPx(480), centralWidget()->width() - scaledPx(96))));
        m_bookmarkPanel->setFixedHeight(qMin(scaledPx(760), qMax(scaledPx(360), centralWidget()->height() - scaledPx(96))));
    }
}

void MainWindow::updateHistoryOverlayGeometry()
{
    if (!m_historyOverlay || !centralWidget()) {
        return;
    }

    m_historyOverlay->setGeometry(centralWidget()->rect());
    if (m_historyPanel) {
        m_historyPanel->setFixedWidth(qMin(scaledPx(820), qMax(scaledPx(480), centralWidget()->width() - scaledPx(96))));
        m_historyPanel->setFixedHeight(qMin(scaledPx(760), qMax(scaledPx(360), centralWidget()->height() - scaledPx(96))));
    }
}

void MainWindow::updateExitOverlayGeometry()
{
    if (!m_exitOverlay || !centralWidget()) {
        return;
    }

    m_exitOverlay->setGeometry(centralWidget()->rect());
    if (m_exitPanel) {
        m_exitPanel->setFixedWidth(qMin(scaledPx(560), qMax(scaledPx(360), centralWidget()->width() - scaledPx(96))));
    }
}

void MainWindow::activateHighlightedHomeItem()
{
    if (auto *button = qobject_cast<QAbstractButton *>(QApplication::focusWidget())) {
        button->click();
        return;
    }

    if (m_recentList && m_recentList->hasFocus() && m_recentList->currentItem()) {
        openSession(m_recentList->currentItem()->data(Qt::UserRole).toString());
        return;
    }

    if (m_bookmarkList && m_bookmarkList->currentItem()) {
        openSite(m_bookmarkList->currentItem()->data(Qt::UserRole).toString());
    }
}

void MainWindow::openSession(const QString &sessionId)
{
    const QString sessionUrl = m_sessionManager->activateSession(sessionId);
    if (sessionUrl.isEmpty()) {
        return;
    }

    syncBookmarkCursor(sessionUrl);
    loadBrowserTab(m_sessionManager->sessionActiveTabId(sessionId), sessionUrl, false);
}

void MainWindow::openUrlInNewSession(const QString &url, const QString &title)
{
    const QString sessionId = m_sessionManager->ensureSiteSession(url, title);
    const QString sessionUrl = m_sessionManager->activateSession(sessionId);
    if (sessionId.isEmpty() || sessionUrl.isEmpty()) {
        return;
    }

    m_sessionManager->updateActiveSession(url, title);
    syncBookmarkCursor(sessionUrl);
    loadBrowserTab(m_sessionManager->sessionActiveTabId(sessionId), url, true);
}

void MainWindow::beginPendingSessionSwitch(const QString &url)
{
    m_pendingSessionSwitchSiteId = m_sessionManager->siteIdForUrl(url);
}

void MainWindow::clearPendingSessionSwitch()
{
    m_pendingSessionSwitchSiteId.clear();
}

bool MainWindow::shouldApplySessionUpdate(const QString &url)
{
    if (m_pendingSessionSwitchSiteId.isEmpty()) {
        return true;
    }

    const QString siteId = m_sessionManager->siteIdForUrl(url);
    if (siteId.isEmpty() || siteId != m_pendingSessionSwitchSiteId) {
        return false;
    }
    return true;
}

void MainWindow::showBookmarksPage()
{
    m_browserController->goHome();
    updatePlaybackInhibit();
    if (m_bookmarkList && m_bookmarkList->count() > 0) {
        m_bookmarkList->setCurrentRow(qBound(0, m_bookmarkCursor, m_bookmarkList->count() - 1));
    }
    if (m_bookmarkList) {
        m_bookmarkList->setFocus();
    }
}

bool MainWindow::navigateActiveDialog(int key)
{
    QWidget *dialog = activeModalDialogWidget();
    if (!dialog) {
        return false;
    }

    if (navigateOrderedDialogFocus(dialog, key)) {
        return true;
    }

    QWidget *focus = QApplication::focusWidget();
    if (!focus || !dialog->isAncestorOf(focus)) {
        cycleDialogFocus(dialog, true);
        return true;
    }

    if (qobject_cast<QListWidget *>(focus) || qobject_cast<QLineEdit *>(focus)) {
        switch (key) {
        case Qt::Key_Up:
        case Qt::Key_Down:
        case Qt::Key_Left:
        case Qt::Key_Right:
            QCoreApplication::postEvent(focus, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
            QCoreApplication::postEvent(focus, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
            return true;
        default:
            return false;
        }
    }

    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Left:
        cycleDialogFocus(dialog, false);
        return true;
    case Qt::Key_Down:
    case Qt::Key_Right:
        cycleDialogFocus(dialog, true);
        return true;
    default:
        return false;
    }
}

bool MainWindow::activateFocusedDialogControl()
{
    QWidget *dialog = activeModalDialogWidget();
    if (!dialog) {
        return false;
    }

    if (auto *button = qobject_cast<QAbstractButton *>(QApplication::focusWidget())) {
        button->click();
        return true;
    }

    if (auto *list = qobject_cast<QListWidget *>(QApplication::focusWidget())) {
        if (list->currentItem()) {
            QCoreApplication::postEvent(list, new QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier));
            QCoreApplication::postEvent(list, new QKeyEvent(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier));
            return true;
        }
    }

    if (auto *lineEdit = qobject_cast<QLineEdit *>(QApplication::focusWidget())) {
        QCoreApplication::postEvent(lineEdit, new QKeyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier));
        QCoreApplication::postEvent(lineEdit, new QKeyEvent(QEvent::KeyRelease, Qt::Key_Return, Qt::NoModifier));
        return true;
    }

    cycleDialogFocus(dialog, true);
    return true;
}

void MainWindow::showHudTemporarily(int durationMs)
{
    cancelTemporaryHudHide();
    m_browserController->setHudVisible(true);
    updateHud();

    if (durationMs <= 0) {
        return;
    }

    const int generation = ++m_hudAutoHideGeneration;
    QTimer::singleShot(durationMs, this, [this, generation]() {
        if (generation != m_hudAutoHideGeneration) {
            return;
        }
        m_browserController->setHudVisible(false);
        updateHud();
    });
}

void MainWindow::cancelTemporaryHudHide()
{
    ++m_hudAutoHideGeneration;
}

void MainWindow::adjustPageZoom(double delta)
{
    if (activeModalDialogWidget() || m_browserController->homeVisible()) {
        return;
    }

    const double currentZoom = m_siteConfig->zoomForUrl(m_browserController->currentUrl());
    const double nextZoom = std::clamp(currentZoom + delta, 0.8, 3.0);
    if (qFuzzyCompare(currentZoom, nextZoom)) {
        return;
    }

    m_siteConfig->setZoomForUrl(m_browserController->currentUrl(), nextZoom);
    m_browserView->setZoomFactor(nextZoom);
    showHudTemporarily(1000);
}

void MainWindow::adjustMinimumFontSize(int delta)
{
    if (activeModalDialogWidget() || m_browserController->homeVisible()) {
        return;
    }

    const int currentSize = m_siteConfig->minimumFontForUrl(m_browserController->currentUrl());
    const int nextSize = std::clamp(currentSize + delta, 12, 28);
    if (currentSize == nextSize) {
        return;
    }

    m_siteConfig->setMinimumFontSizeForUrl(m_browserController->currentUrl(), nextSize);
    m_browserView->setMinimumFontSize(nextSize);
    showHudTemporarily(1000);
}

void MainWindow::closeSiteSession(const QString &siteId)
{
    const QString currentSiteId = m_sessionManager->siteIdForUrl(m_browserController->currentUrl());
    const QString targetSiteId = !siteId.isEmpty()
        ? siteId
        : (!currentSiteId.isEmpty() ? currentSiteId : m_sessionManager->activeSiteId());

    const QList<BrowserSession> targetSessions = m_sessionManager->sessionsForSite(targetSiteId);
    const QString targetSessionId = targetSessions.isEmpty() ? QString() : targetSessions.constFirst().sessionId;

    if (!m_browserController->homeVisible()) {
        if (m_browserView) {
            m_browserView->closeCurrentPage();
        }
        m_browserController->setPageState(QString(), QString(), false, false);
        m_browserController->goHome();
    }

    if (!targetSiteId.isEmpty()) {
        if (!targetSessionId.isEmpty()) {
            const QStringList tabIds = m_sessionManager->tabIdsForSession(targetSessionId);
            enqueueBrowserViewRemovals(tabIds);
        }
        if (m_sessionManager->activeSiteId() == targetSiteId) {
            m_sessionManager->closeActiveSession();
        } else {
            m_sessionManager->closeSessionForSite(targetSiteId);
        }
    }

    updateHud();
    if (m_browserController->homeVisible()) {
        QTimer::singleShot(0, this, [this]() {
            restoreHomeFocus();
        });
        QTimer::singleShot(120, this, [this]() {
            restoreHomeFocus();
        });
    }
}

void MainWindow::handleAction(const QString &action)
{
    if (action == QStringLiteral("back")) {
        handleBackAction();
        return;
    }

    if (action == QStringLiteral("home")) {
        m_browserController->goHome();
        return;
    }

    if (action == QStringLiteral("homeFocusToggle")) {
        toggleHomeFocusSection();
        return;
    }

    if (action == QStringLiteral("bookmark")) {
        if (!m_browserController->currentUrl().isEmpty()) {
            m_bookmarkModel->toggleBookmark(m_browserController->currentTitle(), m_browserController->currentUrl());
            refreshBookmarkList();
        }
        return;
    }

    if (action == QStringLiteral("bookmarks")) {
        showBookmarksPage();
        openBookmarkDialog();
        return;
    }

    if (action == QStringLiteral("menu")) {
        openSettingsDialog();
        return;
    }

    if (action == QStringLiteral("history")) {
        openHistoryDialog();
        return;
    }

    if (action == QStringLiteral("address")) {
        openAddressDialog();
        return;
    }

    if (action == QStringLiteral("refresh")) {
        if (!activeModalDialogWidget() && !m_browserController->homeVisible() && m_browserView) {
            m_browserView->reloadPage();
        }
        return;
    }

    if (action == QStringLiteral("toggleHud")) {
        cancelTemporaryHudHide();
        m_browserController->toggleHud();
        return;
    }

    if (action == QStringLiteral("closeSession")) {
        if (!m_browserController->homeVisible()) {
            QString siteId = m_sessionManager->activeSiteId();
            if (siteId.isEmpty()) {
                siteId = m_sessionManager->siteIdForUrl(m_browserController->currentUrl());
            }
            closeSiteSession(siteId);
        } else {
            closeSiteSession(m_sessionManager->siteIdForUrl(selectedBookmarkUrl()));
        }
        return;
    }

    if (action == QStringLiteral("browserBack")) {
        if (!activeModalDialogWidget() && !m_browserController->homeVisible() && m_browserView && m_browserView->canGoBackNow()) {
            m_browserView->goBack();
        }
        return;
    }

    if (action == QStringLiteral("browserForward")) {
        if (!activeModalDialogWidget() && !m_browserController->homeVisible() && m_browserView && m_browserView->canGoForwardNow()) {
            m_browserView->goForward();
        }
        return;
    }

    if (action == QStringLiteral("zoomIn")) {
        adjustPageZoom(0.1);
        return;
    }

    if (action == QStringLiteral("zoomOut")) {
        adjustPageZoom(-0.1);
        return;
    }

    if (action == QStringLiteral("minFontIn")) {
        adjustMinimumFontSize(1);
        return;
    }

    if (action == QStringLiteral("minFontOut")) {
        adjustMinimumFontSize(-1);
        return;
    }

    if (action == QStringLiteral("previousTab") || action == QStringLiteral("nextTab")) {
        if (activeModalDialogWidget() || m_browserController->homeVisible()) {
            return;
        }

        const QString tabUrl = m_sessionManager->activateAdjacentTab(action == QStringLiteral("nextTab") ? 1 : -1);
        if (tabUrl.isEmpty()) {
            m_statusLabel->setText(QStringLiteral("Only one tab in this session"));
            showHudTemporarily(1000);
            return;
        }

        syncBookmarkCursor(tabUrl);
        loadBrowserTab(m_sessionManager->activeTabId(), tabUrl, false);
        showHudTemporarily(1000);
        return;
    }

    if (action == QStringLiteral("nextSession")) {
        const QString sessionUrl = m_sessionManager->activateAdjacentSession(1);
        if (!sessionUrl.isEmpty()) {
            syncBookmarkCursor(sessionUrl);
            loadBrowserTab(m_sessionManager->activeTabId(), sessionUrl, false);
            showHudTemporarily(1000);
        }
        return;
    }

    if (action == QStringLiteral("previousSession")) {
        const QString sessionUrl = m_sessionManager->activateAdjacentSession(-1);
        if (!sessionUrl.isEmpty()) {
            syncBookmarkCursor(sessionUrl);
            loadBrowserTab(m_sessionManager->activeTabId(), sessionUrl, false);
            showHudTemporarily(1000);
        }
    }
}

void MainWindow::handleBackAction()
{
    if (QWidget *modal = activeModalDialogWidget()) {
        if (auto *dialog = qobject_cast<QDialog *>(modal)) {
            dialog->reject();
            return;
        }

        if (modal == m_addressOverlay) {
            closeAddressOverlay();
            return;
        }
        if (modal == m_bookmarkOverlay) {
            closeBookmarkOverlay();
            return;
        }
        if (modal == m_historyOverlay) {
            closeHistoryOverlay();
            return;
        }
        if (modal == m_settingsOverlay) {
            closeSettingsOverlay();
            return;
        }
        if (modal == m_exitOverlay) {
            closeExitOverlay();
            return;
        }
    }

    if (!m_browserController->homeVisible()) {
        if (m_browserView && m_browserView->canGoBackNow()) {
            m_browserView->goBack();
        } else {
            if (m_sessionManager->activeTabCount() > 1 && m_sessionManager->activeTabIndex() <= 0) {
                m_browserController->goHome();
                return;
            }

            QString closedTabId;
            const QString nextTabUrl = m_sessionManager->closeActiveTab(-1, &closedTabId);

            if (!nextTabUrl.isEmpty() && !m_sessionManager->activeTabId().isEmpty()) {
                loadBrowserTab(m_sessionManager->activeTabId(), nextTabUrl, false);
                showHudTemporarily(1000);
            } else {
                m_browserController->goHome();
            }

            if (!closedTabId.isEmpty()) {
                QTimer::singleShot(0, this, [this, closedTabId]() {
                    removeBrowserView(closedTabId);
                });
            }
        }
        return;
    }

    if (!m_exitOverlay) {
        buildExitOverlay();
    }
    if (!m_exitOverlay) {
        return;
    }

    updateExitOverlayGeometry();
    m_exitOverlay->show();
    m_exitOverlay->raise();
    m_exitPanel->raise();
    if (m_exitConfirmButton) {
        m_exitConfirmButton->setFocus();
    }
}

int MainWindow::bookmarkIndexForUrl(const QString &url) const
{
    const QString normalized = QUrl::fromUserInput(url.trimmed()).toString(QUrl::RemoveFragment);
    if (normalized.isEmpty()) {
        return -1;
    }

    const QString siteId = m_sessionManager ? m_sessionManager->siteIdForUrl(normalized) : QString();

    for (int row = 0; row < m_bookmarkModel->count(); ++row) {
        if (m_bookmarkModel->urlAt(row) == normalized) {
            return row;
        }

        if (!siteId.isEmpty() && m_sessionManager->siteIdForUrl(m_bookmarkModel->urlAt(row)) == siteId) {
            return row;
        }
    }

    return -1;
}

void MainWindow::syncBookmarkCursor(const QString &url)
{
    const int bookmarkIndex = bookmarkIndexForUrl(url);
    if (bookmarkIndex < 0) {
        return;
    }

    m_bookmarkCursor = bookmarkIndex;
    if (m_bookmarkList && m_bookmarkList->count() > bookmarkIndex) {
        m_bookmarkList->setCurrentRow(bookmarkIndex);
    }
}

QString MainWindow::selectedBookmarkUrl() const
{
    const int currentRow = m_bookmarkList ? m_bookmarkList->currentRow() : -1;
    if (currentRow >= 0) {
        return m_bookmarkModel->urlAt(currentRow);
    }

    return m_bookmarkModel->urlAt(m_bookmarkCursor);
}

QWidget *MainWindow::normalizedHomeFocusWidget() const
{
    QWidget *widget = QApplication::focusWidget();
    if (!widget) {
        return nullptr;
    }

    if (widget == m_bookmarkList || (m_bookmarkList && m_bookmarkList->isAncestorOf(widget))) {
        return m_bookmarkList;
    }
    if (widget == m_recentList || (m_recentList && m_recentList->isAncestorOf(widget))) {
        return m_recentList;
    }
    if (widget == m_homeAddressButton || (m_homeAddressButton && m_homeAddressButton->isAncestorOf(widget))) {
        return m_homeAddressButton;
    }
    if (widget == m_homeHistoryButton || (m_homeHistoryButton && m_homeHistoryButton->isAncestorOf(widget))) {
        return m_homeHistoryButton;
    }
    if (widget == m_homeSettingsButton || (m_homeSettingsButton && m_homeSettingsButton->isAncestorOf(widget))) {
        return m_homeSettingsButton;
    }

    return nullptr;
}

QPushButton *MainWindow::currentHomeTopButton() const
{
    QWidget *focus = normalizedHomeFocusWidget();
    if (focus == m_homeAddressButton) {
        return m_homeAddressButton;
    }
    if (focus == m_homeHistoryButton) {
        return m_homeHistoryButton;
    }
    if (focus == m_homeSettingsButton) {
        return m_homeSettingsButton;
    }
    return nullptr;
}

QList<QWidget *> MainWindow::orderedDialogFocusWidgets(QWidget *dialog) const
{
    QList<QWidget *> widgets;
    const auto appendIfFocusable = [&widgets](QWidget *widget) {
        if (widget && widget->focusPolicy() != Qt::NoFocus) {
            widgets.append(widget);
        }
    };

    if (dialog == m_addressOverlay) {
        appendIfFocusable(m_addressLineEdit);
        appendIfFocusable(m_addressOpenButton);
        appendIfFocusable(m_addressCancelButton);
    } else if (dialog == m_bookmarkOverlay) {
        appendIfFocusable(m_bookmarkDialogList);
        appendIfFocusable(m_bookmarkToggleButton);
        appendIfFocusable(m_bookmarkCloseButton);
    } else if (dialog == m_historyOverlay) {
        appendIfFocusable(m_historyDialogList);
        appendIfFocusable(m_historyCloseButton);
    } else if (dialog == m_settingsOverlay) {
        for (QWidget *widget : m_settingsNavigationWidgets) {
            appendIfFocusable(widget);
        }
    } else if (dialog == m_exitOverlay) {
        appendIfFocusable(m_exitConfirmButton);
        appendIfFocusable(m_exitCancelButton);
    }

    return widgets;
}

bool MainWindow::navigateOrderedDialogFocus(QWidget *dialog, int key)
{
    const QList<QWidget *> widgets = orderedDialogFocusWidgets(dialog);
    if (widgets.isEmpty()) {
        return false;
    }

    QWidget *focus = QApplication::focusWidget();
    int currentIndex = -1;
    for (int index = 0; index < widgets.size(); ++index) {
        QWidget *widget = widgets.at(index);
        if (focus == widget || (focus && widget->isAncestorOf(focus))) {
            currentIndex = index;
            break;
        }
    }

    if (currentIndex < 0) {
        widgets.constFirst()->setFocus();
        return true;
    }

    QWidget *current = widgets.at(currentIndex);
    if (qobject_cast<QListWidget *>(current) || qobject_cast<QLineEdit *>(current)) {
        return false;
    }

    int nextIndex = currentIndex;
    switch (key) {
    case Qt::Key_Up:
    case Qt::Key_Left:
        nextIndex = qMax(0, currentIndex - 1);
        break;
    case Qt::Key_Down:
    case Qt::Key_Right:
        nextIndex = qMin(widgets.size() - 1, currentIndex + 1);
        break;
    default:
        return false;
    }

    widgets.at(nextIndex)->setFocus();
    return true;
}

bool MainWindow::toggleHomeFocusSection()
{
    if (!m_browserController->homeVisible() || activeModalDialogWidget()) {
        return false;
    }

    const auto clearHomeReentryTargets = [this]() {
        m_homeBookmarkReentryRow = -1;
        m_homeRecentReentryRow = -1;
    };

    const auto focusTopButton = [](QPushButton *button) {
        if (!button || !button->isEnabled()) {
            return false;
        }
        button->setFocus();
        return true;
    };

    const auto focusListRow = [](QListWidget *list, int preferredRow) {
        if (!list || list->count() <= 0) {
            return false;
        }

        const int row = qBound(0, preferredRow < 0 ? 0 : preferredRow, list->count() - 1);
        list->setCurrentRow(row);
        list->setFocus();
        return true;
    };

    const auto preferredReentryRow = [this](QListWidget *list, int fallbackRow) {
        if (list == m_bookmarkList && m_homeBookmarkReentryRow >= 0) {
            const int row = m_homeBookmarkReentryRow;
            m_homeBookmarkReentryRow = -1;
            return row;
        }
        if (list == m_recentList && m_homeRecentReentryRow >= 0) {
            const int row = m_homeRecentReentryRow;
            m_homeRecentReentryRow = -1;
            return row;
        }
        return fallbackRow;
    };

    QWidget *focus = normalizedHomeFocusWidget();
    if (!focus) {
        clearHomeReentryTargets();
        restoreHomeFocus();
        return true;
    }

    if (focus == m_bookmarkList) {
        m_homeBookmarkReentryRow = m_bookmarkList ? qMax(0, m_bookmarkList->currentRow()) : qMax(0, m_bookmarkCursor);
        m_homeRecentReentryRow = -1;
        const bool changed = focusTopButton(m_homeAddressButton)
            || focusTopButton(m_homeHistoryButton)
            || focusTopButton(m_homeSettingsButton);
        updateHomeSectionVisualState();
        return changed;
    }

    if (focus == m_recentList) {
        m_homeRecentReentryRow = m_recentList ? qMax(0, m_recentList->currentRow()) : 0;
        m_homeBookmarkReentryRow = -1;
        const bool changed = focusTopButton(m_homeHistoryButton)
            || focusTopButton(m_homeSettingsButton)
            || focusTopButton(m_homeAddressButton);
        updateHomeSectionVisualState();
        return changed;
    }

    if (focus == m_homeAddressButton) {
        const bool changed = focusListRow(m_bookmarkList, preferredReentryRow(m_bookmarkList, m_bookmarkList ? m_bookmarkList->currentRow() : m_bookmarkCursor))
            || focusListRow(m_recentList, preferredReentryRow(m_recentList, m_recentList ? m_recentList->currentRow() : 0));
        updateHomeSectionVisualState();
        return changed;
    }

    const bool changed = focusListRow(m_recentList, preferredReentryRow(m_recentList, m_recentList ? m_recentList->currentRow() : 0))
        || focusListRow(m_bookmarkList, preferredReentryRow(m_bookmarkList, m_bookmarkList ? m_bookmarkList->currentRow() : m_bookmarkCursor));
    updateHomeSectionVisualState();
    return changed;
}

bool MainWindow::handleHomeNavigationKey(int key)
{
    if (!m_browserController->homeVisible() || activeModalDialogWidget()) {
        return false;
    }

    QWidget *focus = normalizedHomeFocusWidget();
    if (!focus) {
        return false;
    }

    const auto focusTopButton = [](QPushButton *button) {
        if (!button || !button->isEnabled()) {
            return false;
        }
        button->setFocus();
        return true;
    };

    const auto focusListRow = [](QListWidget *list, int preferredRow) {
        if (!list || list->count() <= 0) {
            return false;
        }

        const int row = qBound(0, preferredRow < 0 ? 0 : preferredRow, list->count() - 1);
        list->setCurrentRow(row);
        list->setFocus();
        return true;
    };

    if (focus == m_homeAddressButton || focus == m_homeHistoryButton || focus == m_homeSettingsButton) {
        switch (key) {
        case Qt::Key_Up:
        case Qt::Key_Down:
            return true;
        case Qt::Key_Left:
            if (focus == m_homeHistoryButton) {
                return focusTopButton(m_homeAddressButton);
            }
            if (focus == m_homeSettingsButton) {
                return focusTopButton(m_homeHistoryButton);
            }
            return true;
        case Qt::Key_Right:
            if (focus == m_homeAddressButton) {
                return focusTopButton(m_homeHistoryButton);
            }
            if (focus == m_homeHistoryButton) {
                return focusTopButton(m_homeSettingsButton);
            }
            return true;
        default:
            return false;
        }
    }

    auto *list = qobject_cast<QListWidget *>(focus);
    if (!list) {
        return false;
    }

    const int currentRow = qMax(0, list->currentRow());
    switch (key) {
    case Qt::Key_Up:
        if (list->count() > 0 && currentRow > 0) {
            if (list == m_bookmarkList) {
                m_homeBookmarkReentryRow = -1;
            } else if (list == m_recentList) {
                m_homeRecentReentryRow = -1;
            }
            list->setCurrentRow(currentRow - 1);
            return true;
        }
        return true;
    case Qt::Key_Down:
        if (list->count() <= 0) {
            return true;
        }
        if (list == m_bookmarkList) {
            m_homeBookmarkReentryRow = -1;
        } else if (list == m_recentList) {
            m_homeRecentReentryRow = -1;
        }
        list->setCurrentRow(qMin(list->count() - 1, currentRow + 1));
        return true;
    case Qt::Key_Left:
        if (list == m_recentList) {
            return focusListRow(m_bookmarkList, currentRow) || true;
        }
        return true;
    case Qt::Key_Right:
        if (list == m_bookmarkList) {
            return focusListRow(m_recentList, currentRow) || true;
        }
        return true;
    default:
        return false;
    }
}

void MainWindow::updateHomeSectionVisualState()
{
    const bool homeVisible = m_browserController && m_browserController->homeVisible() && !activeModalDialogWidget();
    QWidget *focus = homeVisible ? normalizedHomeFocusWidget() : nullptr;
    rememberHomeTopButtonFocus(focus);
    const bool topActive = focus == m_homeAddressButton || focus == m_homeHistoryButton || focus == m_homeSettingsButton;
    const bool listsActive = focus == m_bookmarkList || focus == m_recentList;

    const QList<QWidget *> topWidgets = {m_homeAddressButton, m_homeHistoryButton, m_homeSettingsButton};
    for (QWidget *widget : topWidgets) {
        if (!widget) {
            continue;
        }
        widget->setProperty("homeSectionActive", topActive);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }

    const QList<QWidget *> listWidgets = {m_bookmarkList, m_recentList};
    for (QWidget *widget : listWidgets) {
        if (!widget) {
            continue;
        }
        widget->setProperty("homeSectionActive", listsActive);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

void MainWindow::rememberHomeTopButtonFocus(QWidget *focus)
{
    QWidget *target = focus ? focus : normalizedHomeFocusWidget();
    if (target == m_homeAddressButton) {
        m_lastHomeTopButtonIndex = 0;
    } else if (target == m_homeHistoryButton) {
        m_lastHomeTopButtonIndex = 1;
    } else if (target == m_homeSettingsButton) {
        m_lastHomeTopButtonIndex = 2;
    }
}

bool MainWindow::restoreRememberedHomeTopButtonFocus()
{
    QPushButton *button = nullptr;
    switch (m_lastHomeTopButtonIndex) {
    case 1:
        button = m_homeHistoryButton;
        break;
    case 2:
        button = m_homeSettingsButton;
        break;
    case 0:
    default:
        button = m_homeAddressButton;
        break;
    }

    if (!button || !button->isVisible() || !button->isEnabled()) {
        return false;
    }

    button->setFocus();
    updateHomeSectionVisualState();
    return true;
}

void MainWindow::updateHud()
{
    const UiScaleReading systemUiScale = detectSystemUiScale(this);
    const QSize displayResolution = currentDisplayResolution(this);
    const int hudWidth = qMin(scaledPx(980), qMax(scaledPx(420), m_browserPage->width() - scaledPx(48)));
    const int hudPadding = scaledPx(24);
    const int hudSpacing = scaledPx(8);
    const int listColumnWidth = qMax(scaledPx(160), (hudWidth - (hudPadding * 2) - hudSpacing) / 2);
    const int listBodyLines = 4;
    const QString qtScale = qtScaleSource();
    const qreal appUiScale = m_siteConfig->appUiScale();
    const qreal pageZoom = m_siteConfig->zoomForUrl(m_browserController->currentUrl());
    const int minimumFontSize = m_siteConfig->minimumFontForUrl(m_browserController->currentUrl());
    const int tabCount = m_sessionManager->activeTabCount();
    const int tabIndex = m_sessionManager->activeTabIndex();
    const QString tabText = tabCount > 0
        ? QStringLiteral("Tab: %1/%2").arg(tabIndex >= 0 ? tabIndex + 1 : 1).arg(tabCount)
        : QStringLiteral("Tab: -");
    const QString scaleDebugText = QStringLiteral("Qt: %1  System: %2x  App: %3x  Res: %4x%5  Page: %6x  Min Font: %7px")
                                       .arg(qtScale,
                                        QString::number(systemUiScale.value, 'f', 2),
                                            QString::number(appUiScale, 'f', 2),
                                            QString::number(displayResolution.width()),
                                            QString::number(displayResolution.height()),
                                            QString::number(pageZoom, 'f', 2),
                                            QString::number(minimumFontSize));

    m_titleLabel->setText(m_browserController->currentTitle().isEmpty() ? QStringLiteral("Ready") : m_browserController->currentTitle());
    m_urlLabel->setText(m_browserController->currentUrl().isEmpty() ? QStringLiteral("Home") : m_browserController->currentUrl());
                m_modeLabel->setText(QStringLiteral("Input: left stick cursor  D-pad arrows  %1  System UI: %2x  App UI: %3x  Res: %4x%5  Page Zoom: %6x  Min Font: %7px")
                             .arg(tabText,
                                QString::number(systemUiScale.value, 'f', 2),
                                  QString::number(appUiScale, 'f', 2),
                                                                    QString::number(displayResolution.width()),
                                                                    QString::number(displayResolution.height()),
                                                                    QString::number(pageZoom, 'f', 2),
                                                                    QString::number(minimumFontSize)));
    m_statusLabel->setText(QStringLiteral("A: click/end drag  LB+A: right click  LT+A: hold drag  B: back/home  Hold B: close site  LB/RB: switch tab  D-pad: repeat nav  LB+D-pad: back/fwd  RB+D-pad: zoom/font  LT/RT: switch site"));
    const QStringList tabSummaries = m_sessionManager->activeTabSummaries();
    const QStringList sessionSummaries = m_sessionManager->sessionSummaries();
    const QFontMetrics listMetrics(m_tabListLabel ? m_tabListLabel->font() : font());
    const int listHeight = listMetrics.lineSpacing() * (listBodyLines + 1);
    if (m_sessionListLabel) {
        m_sessionListLabel->setFixedWidth(listColumnWidth);
        m_sessionListLabel->setFixedHeight(listHeight);
        m_sessionListLabel->setText(elideHudLines(QStringLiteral("Sessions"),
                                                  sessionSummaries.isEmpty() ? QStringList{QStringLiteral("-")} : sessionSummaries,
                                                  listMetrics,
                                                  listColumnWidth,
                                                  listBodyLines));
    }
    if (m_tabListLabel) {
        m_tabListLabel->setFixedWidth(listColumnWidth);
        m_tabListLabel->setFixedHeight(listHeight);
        m_tabListLabel->setText(elideHudLines(QStringLiteral("Tabs"),
                                              tabSummaries.isEmpty() ? QStringList{QStringLiteral("-")} : tabSummaries,
                                              listMetrics,
                                              listColumnWidth,
                                              listBodyLines));
    }
    if (m_homeDebugLabel) {
        m_homeDebugLabel->setText(scaleDebugText);
    }

    m_hud->setFixedWidth(hudWidth);
    m_hud->adjustSize();
    m_hud->move(scaledPx(24), scaledPx(24));
    m_hud->setVisible(m_browserController->hudVisible() && !m_browserController->homeVisible());
    m_hud->raise();
}

void MainWindow::restoreHomeFocus()
{
    if (!m_browserController->homeVisible()) {
        return;
    }

    if (activeModalDialogWidget()) {
        return;
    }

    if (m_bookmarkList && m_bookmarkList->count() > 0) {
        m_bookmarkList->setCurrentRow(qBound(0, m_bookmarkCursor, m_bookmarkList->count() - 1));
        m_bookmarkList->setFocus();
        updateHomeSectionVisualState();
        return;
    }

    if (m_recentList && m_recentList->count() > 0) {
        m_recentList->setCurrentRow(qMax(0, m_recentList->currentRow()));
        m_recentList->setFocus();
        updateHomeSectionVisualState();
        return;
    }

    if (restoreRememberedHomeTopButtonFocus()) {
        return;
    }

    const QList<QPushButton *> buttons = m_homePage ? m_homePage->findChildren<QPushButton *>() : QList<QPushButton *>();
    for (QPushButton *button : buttons) {
        if (button && button->isVisible() && button->isEnabled()) {
            button->setFocus();
            updateHomeSectionVisualState();
            return;
        }
    }

    if (m_homePage) {
        m_homePage->setFocus();
    }

    updateHomeSectionVisualState();
}

void MainWindow::updatePointerMode()
{
    const bool homeVisible = m_browserController->homeVisible();
    m_inputManager->setHomeNavigationEnabled(homeVisible);

    m_inputManager->setSystemCursorVisible(!homeVisible);
    applyPointerCursor(true);
    updateTouchDragIndicator();
    updateAddressOverlayGeometry();
    updateBookmarkOverlayGeometry();
    updateHistoryOverlayGeometry();
    updateSettingsOverlayGeometry();
    updateExitOverlayGeometry();
}

void MainWindow::updatePlaybackInhibit()
{
    const bool browserFrontmost = qGuiApp->applicationState() == Qt::ApplicationActive
        && !m_browserController->homeVisible()
        && m_stack
        && m_stack->currentWidget() == m_browserPage
        && m_browserView;
    const bool mediaPlaybackActive = browserFrontmost && m_browserView->isMediaPlaybackActive();

    QStringList triggerReasons;
    QStringList releaseReasons;
    if (browserFrontmost && m_browserView) {
        if (m_browserView->isAudiblePlaybackActive()) {
            triggerReasons.append(QStringLiteral("audio"));
        }
        if (m_browserView->isVideoPlaybackActive()) {
            triggerReasons.append(QStringLiteral("video"));
        }
        if (m_browserView->isPictureInPicturePlaybackActive()) {
            triggerReasons.append(QStringLiteral("picture-in-picture"));
        }
        if (m_browserView->isFullscreenVideoPlaybackActive()) {
            triggerReasons.append(QStringLiteral("fullscreen-video"));
        }
    } else {
        if (qGuiApp->applicationState() != Qt::ApplicationActive) {
            releaseReasons.append(QStringLiteral("app-inactive"));
        }
        if (m_browserController->homeVisible()) {
            releaseReasons.append(QStringLiteral("home-visible"));
        }
        if (!m_browserView) {
            releaseReasons.append(QStringLiteral("no-browser-view"));
        } else if (m_stack && m_stack->currentWidget() != m_browserPage) {
            releaseReasons.append(QStringLiteral("browser-page-not-frontmost"));
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

void MainWindow::updateTouchDragIndicator()
{
    if (!m_touchDragIndicator || !m_browserPage || !m_browserView || m_browserController->homeVisible() || !m_inputManager->dragLockActive()) {
        if (m_touchDragIndicator) {
            m_touchDragIndicator->hide();
        }
        return;
    }

    const QPoint browserLocal = m_browserView->mapFromGlobal(m_inputManager->cursorPosition().toPoint());
    const QPoint pageLocal = m_browserPage->mapFrom(m_browserView, browserLocal);
    const int halfWidth = m_touchDragIndicator->width() / 2;
    const int halfHeight = m_touchDragIndicator->height() / 2;
    const int x = qBound(0, pageLocal.x() - halfWidth, qMax(0, m_browserPage->width() - m_touchDragIndicator->width()));
    const int y = qBound(0, pageLocal.y() - halfHeight, qMax(0, m_browserPage->height() - m_touchDragIndicator->height()));
    m_touchDragIndicator->move(x, y);
    m_touchDragIndicator->show();
    m_touchDragIndicator->raise();
}

QString MainWindow::semanticActionForKey(int key) const
{
    if (key == Qt::Key_Escape || key == Qt::Key_Back) {
        return QStringLiteral("back");
    }
    if (key == Qt::Key_Q) {
        return QStringLiteral("previousSession");
    }
    if (key == Qt::Key_E) {
        return QStringLiteral("nextSession");
    }
    if (key == Qt::Key_F) {
        return QStringLiteral("bookmarks");
    }
    if (key == Qt::Key_M || key == Qt::Key_Menu) {
        return QStringLiteral("home");
    }
    if (key == Qt::Key_H || key == Qt::Key_Backspace) {
        return QStringLiteral("history");
    }
    if (key == Qt::Key_Slash) {
        return QStringLiteral("address");
    }
    if (key == Qt::Key_F1) {
        return QStringLiteral("toggleHud");
    }
    if (key == Qt::Key_Z) {
        return QStringLiteral("previousTab");
    }
    if (key == Qt::Key_C) {
        return QStringLiteral("nextTab");
    }
    if (key == Qt::Key_PageDown) {
        return QStringLiteral("nextSession");
    }
    if (key == Qt::Key_PageUp) {
        return QStringLiteral("previousSession");
    }
    return {};
}