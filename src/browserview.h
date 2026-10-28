#pragma once

#include <QImage>
#include <QQuickPaintedItem>

#include <include/cef_render_handler.h>

#include <memory>

class BrowserViewClient;
class QInputMethodEvent;
class QQuickWindow;

class BrowserView : public QQuickPaintedItem
{
    Q_OBJECT
    friend class BrowserViewClient;
    friend class BrowserViewTestAccess;

public:
    explicit BrowserView(QQuickItem *parent = nullptr);
    ~BrowserView() override;

    QString currentUrl() const;
    QString currentTitle() const;
    bool canGoBack() const;
    bool canGoForward() const;
    bool canGoBackNow() const;
    bool canGoForwardNow() const;
    double zoomFactor() const;
    bool hasLiveBrowser() const;
    bool isBrowserClosing() const;
    bool isFullscreenMode() const;
    bool isAudiblePlaybackActive() const;
    bool isVideoPlaybackActive() const;
    bool isPictureInPicturePlaybackActive() const;
    bool isFullscreenVideoPlaybackActive() const;
    bool isMediaPlaybackActive() const;
    bool isLoading() const;
    QImage captureViewImage() const;
    bool hasFrame() const;

    void setBrowserInputEnabled(bool enabled);
    void setUiScaleFactor(qreal factor);
    qreal uiScaleFactor() const;
    bool isViewActive() const;
    void setTabActive(bool active);

public slots:
    void loadUrl(const QString &url);
    void closeCurrentPage();
    void goBack();
    void goForward();
    void reloadPage();
    void setZoomFactor(double factor);
    void setMinimumFontSize(int size);
    void setAudioMuted(bool muted);
    void requestNativeFocus();
    void syncPointerFromGlobal(const QPoint &globalPos, Qt::MouseButtons buttons = Qt::NoButton);
    void sendMouseButtonFromGlobal(const QPoint &globalPos,
                                   Qt::MouseButton button,
                                   bool mouseUp,
                                   Qt::MouseButtons buttons);
    void beginTouchDragFromGlobal(const QPoint &globalPos);
    void updateTouchDragFromGlobal(const QPoint &globalPos);
    void endTouchDragFromGlobal(const QPoint &globalPos);
    void setNativeCursorVisible(bool visible);
    void scrollByPixels(const QPoint &delta, const QPoint &globalPos);
    bool containsGlobalPoint(const QPoint &globalPos) const;
    void sendWheelStep(const QPoint &angleDelta);
    void sendNavigationKey(int key);
    void commitOskText(const QString &text);
    void forwardInputMethodEvent(QInputMethodEvent *event);

signals:
    void addressChanged(const QString &url);
    void titleChanged(const QString &title);
    void navigationStateChanged(bool canGoBack, bool canGoForward);
    void firstFrameAvailable();
    void loadSucceeded(const QString &url, const QString &title);
    void loadFailed(const QString &url, int errorCode, const QString &errorText);
    void loadingStateChanged(bool isLoading);
    void browserClosed();
    void fullscreenModeChanged(bool fullscreen);
    void newTabRequested(const QString &url);
    void renderProcessTerminated(int status, int errorCode, const QString &errorText);
    void tabPressed();
    void audiblePlaybackChanged(bool active);
    void mediaPlaybackChanged(bool active);
    void virtualKeyboardRequested(bool requested);

protected:
    void paint(QPainter *painter) override;
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void inputMethodEvent(QInputMethodEvent *event) override;
    void focusInEvent(QFocusEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

private:
    void handleAddressSignal(const QString &url);
    void handleTitleSignal(const QString &title);
    void handleNavigationSignal(bool canGoBack, bool canGoForward);
    void handleBrowserClosed();
    void armFrameWatchdog();
    void stopFrameWatchdog();
    void onFrameWatchdogTimeout();
    void refreshNativeBrowserWindowHints();
    CefRect viewRectForOsr() const;
    bool fillScreenInfoForOsr(CefScreenInfo &screenInfo) const;
    bool mapPointToScreenForOsr(int viewX, int viewY, int &screenX, int &screenY) const;
    void handlePopupVisibility(bool visible);
    void handlePopupRectChanged(const CefRect &rect);
    void handleImeCompositionRangeChanged(const CefRenderHandler::RectList &characterBounds);
    void handleOsrPaint(CefRenderHandler::PaintElementType type,
                        const CefRenderHandler::RectList &dirtyRects,
                        const QImage &frame);
    void scheduleCreateBrowserIfNeeded();
    void createBrowserIfNeeded();
    void resizeBrowserToView();
    void applyVisibleState(bool visible);
    void bindQuickWindow(QQuickWindow *quickWindow);
    void sendMouseMoveToBrowser(const QPoint &position, Qt::MouseButtons buttons = Qt::NoButton);
    void sendMouseClickToBrowser(const QPoint &position, Qt::MouseButton button, bool mouseUp, Qt::MouseButtons buttons);
    void sendKeyEventToBrowser(QKeyEvent *event, bool keyUp);
    QPoint mapPointFromGlobal(const QPoint &globalPos) const;
    QPoint itemToCefPoint(const QPoint &itemPos) const;
    QRectF cefRectToItemRect(const QRect &cefRect) const;
    QSize cefViewSize() const;
    qreal viewDevicePixelRatio() const;
    qreal effectiveUiScale() const;
    void setAudiblePlaybackActive(bool active);
    void setVideoPlaybackState(const QString &frameId,
                               bool videoActive,
                               bool pictureInPictureActive,
                               bool fullscreenVideoActive);
    void clearVideoPlaybackState();

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
