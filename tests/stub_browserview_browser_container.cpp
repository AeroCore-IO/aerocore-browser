#include "browserview.h"
#include "browserview_test_access.h"

#include <QFocusEvent>
#include <QHoverEvent>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QQuickWindow>
#include <QVariant>
#include <QWheelEvent>

struct BrowserView::Impl {
    QString currentUrl;
    QString currentTitle;
    bool canGoBack = false;
    bool canGoForward = false;
    bool isLoading = false;
    bool hasFrame = false;
    bool notifyNextFrame = false;
    bool browserClosing = false;
    bool fullscreenMode = false;
    double zoomFactor = 1.2;
    int minimumFontSize = 20;
    qreal uiScaleFactor = 1.0;
    bool tabActive = false;
    int mouseButtonEventCount = 0;
    bool *lifetime = nullptr;
    QMetaObject::Connection windowChangedConnection;
};

BrowserView::BrowserView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_impl(std::make_unique<Impl>())
{
    m_impl->lifetime = new bool(true);
    // Mirrors production: windowChanged must not run after m_impl is destroyed.
    m_impl->windowChangedConnection =
        connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *) {
            Q_ASSERT(m_impl);
            Q_ASSERT(m_impl->lifetime);
            Q_ASSERT(*m_impl->lifetime);
        });
}

BrowserView::~BrowserView()
{
    if (m_impl->windowChangedConnection) {
        disconnect(m_impl->windowChangedConnection);
        m_impl->windowChangedConnection = {};
    }
    if (m_impl->lifetime) {
        *m_impl->lifetime = false;
        delete m_impl->lifetime;
        m_impl->lifetime = nullptr;
    }
}

void BrowserView::setBrowserInputEnabled(bool enabled)
{
    Q_UNUSED(enabled);
}

void BrowserView::setUiScaleFactor(qreal factor)
{
    m_impl->uiScaleFactor = factor;
}

qreal BrowserView::uiScaleFactor() const
{
    return m_impl->uiScaleFactor;
}

QString BrowserView::currentUrl() const
{
    return m_impl->currentUrl;
}

QString BrowserView::currentTitle() const
{
    return m_impl->currentTitle;
}

bool BrowserView::canGoBack() const
{
    return m_impl->canGoBack;
}

bool BrowserView::canGoForward() const
{
    return m_impl->canGoForward;
}

bool BrowserView::canGoBackNow() const
{
    return m_impl->canGoBack;
}

bool BrowserView::canGoForwardNow() const
{
    return m_impl->canGoForward;
}

double BrowserView::zoomFactor() const
{
    return m_impl->zoomFactor;
}

bool BrowserView::hasLiveBrowser() const
{
    return false;
}

bool BrowserView::isBrowserClosing() const
{
    return m_impl->browserClosing;
}

bool BrowserView::isFullscreenMode() const
{
    return m_impl->fullscreenMode;
}

bool BrowserView::isAudiblePlaybackActive() const
{
    return false;
}

bool BrowserView::isVideoPlaybackActive() const
{
    return false;
}

bool BrowserView::isPictureInPicturePlaybackActive() const
{
    return false;
}

bool BrowserView::isFullscreenVideoPlaybackActive() const
{
    return false;
}

bool BrowserView::isMediaPlaybackActive() const
{
    return false;
}

bool BrowserView::isLoading() const
{
    return m_impl->isLoading;
}

QImage BrowserView::captureViewImage() const
{
    return {};
}

void BrowserView::loadUrl(const QString &url)
{
    m_impl->currentUrl = url;
    m_impl->notifyNextFrame = true;
    if (!m_impl->isLoading) {
        m_impl->isLoading = true;
        emit loadingStateChanged(true);
    }
}

void BrowserView::closeCurrentPage()
{
    m_impl->browserClosing = true;
    emit browserClosed();
}

void BrowserView::goBack()
{
}

void BrowserView::goForward()
{
}

void BrowserView::reloadPage()
{
}

void BrowserView::setZoomFactor(double factor)
{
    m_impl->zoomFactor = factor;
}

void BrowserView::setMinimumFontSize(int size)
{
    m_impl->minimumFontSize = size;
}

void BrowserView::setAudioMuted(bool muted)
{
    Q_UNUSED(muted);
}

bool BrowserView::hasFrame() const
{
    return m_impl->hasFrame;
}

bool BrowserView::isViewActive() const
{
    return m_impl->tabActive;
}

void BrowserView::setTabActive(bool active)
{
    m_impl->tabActive = active;
}

void BrowserView::requestNativeFocus()
{
    forceActiveFocus(Qt::OtherFocusReason);
}

void BrowserView::syncPointerFromGlobal(const QPoint &globalPos, Qt::MouseButtons buttons)
{
    Q_UNUSED(globalPos);
    Q_UNUSED(buttons);
}

void BrowserView::sendMouseButtonFromGlobal(const QPoint &globalPos,
                                            Qt::MouseButton button,
                                            bool mouseUp,
                                            Qt::MouseButtons buttons)
{
    Q_UNUSED(globalPos);
    Q_UNUSED(button);
    Q_UNUSED(mouseUp);
    Q_UNUSED(buttons);
    ++m_impl->mouseButtonEventCount;
}

void BrowserView::beginTouchDragFromGlobal(const QPoint &globalPos)
{
    Q_UNUSED(globalPos);
}

void BrowserView::updateTouchDragFromGlobal(const QPoint &globalPos)
{
    Q_UNUSED(globalPos);
}

void BrowserView::endTouchDragFromGlobal(const QPoint &globalPos)
{
    Q_UNUSED(globalPos);
}

void BrowserView::setNativeCursorVisible(bool visible)
{
    Q_UNUSED(visible);
}

void BrowserView::scrollByPixels(const QPoint &delta, const QPoint &globalPos)
{
    Q_UNUSED(delta);
    Q_UNUSED(globalPos);
}

bool BrowserView::containsGlobalPoint(const QPoint &globalPos) const
{
    Q_UNUSED(globalPos);
    return true;
}

void BrowserView::sendWheelStep(const QPoint &angleDelta)
{
    Q_UNUSED(angleDelta);
}

void BrowserView::sendNavigationKey(int key)
{
    Q_UNUSED(key);
}

void BrowserView::commitOskText(const QString &text)
{
    Q_UNUSED(text);
}

void BrowserView::forwardInputMethodEvent(QInputMethodEvent *event)
{
    Q_UNUSED(event);
}

void BrowserView::paint(QPainter *painter)
{
    Q_UNUSED(painter);
}

void BrowserView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
}

void BrowserView::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickPaintedItem::itemChange(change, value);
}

void BrowserView::keyPressEvent(QKeyEvent *event)
{
    QQuickPaintedItem::keyPressEvent(event);
}

void BrowserView::keyReleaseEvent(QKeyEvent *event)
{
    QQuickPaintedItem::keyReleaseEvent(event);
}

void BrowserView::inputMethodEvent(QInputMethodEvent *event)
{
    Q_UNUSED(event);
}

QVariant BrowserView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    return QQuickPaintedItem::inputMethodQuery(query);
}

void BrowserView::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
}

void BrowserView::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);
}

void BrowserView::hoverMoveEvent(QHoverEvent *event)
{
    QQuickPaintedItem::hoverMoveEvent(event);
}

void BrowserView::mouseMoveEvent(QMouseEvent *event)
{
    QQuickPaintedItem::mouseMoveEvent(event);
}

void BrowserView::mousePressEvent(QMouseEvent *event)
{
    QQuickPaintedItem::mousePressEvent(event);
}

void BrowserView::mouseReleaseEvent(QMouseEvent *event)
{
    QQuickPaintedItem::mouseReleaseEvent(event);
}

void BrowserView::wheelEvent(QWheelEvent *event)
{
    QQuickPaintedItem::wheelEvent(event);
}

void BrowserViewTestAccess::setLoading(BrowserView *view, bool loading)
{
    if (!view || !view->m_impl) {
        return;
    }

    if (view->m_impl->isLoading == loading) {
        return;
    }

    view->m_impl->isLoading = loading;
    emit view->loadingStateChanged(loading);
}

void BrowserViewTestAccess::setHasFrame(BrowserView *view, bool hasFrame)
{
    if (!view || !view->m_impl) {
        return;
    }

    if (view->m_impl->hasFrame == hasFrame) {
        return;
    }

    view->m_impl->hasFrame = hasFrame;
}

bool BrowserViewTestAccess::notifyNextFrameArmed(const BrowserView *view)
{
    return view && view->m_impl && view->m_impl->notifyNextFrame;
}

void BrowserViewTestAccess::deliverPendingFrameNotification(BrowserView *view)
{
    if (!view || !view->m_impl) {
        return;
    }

    if (!view->m_impl->notifyNextFrame && !view->m_impl->hasFrame) {
        return;
    }

    view->m_impl->notifyNextFrame = false;
    view->m_impl->hasFrame = true;
    emit view->firstFrameAvailable();
}

int BrowserViewTestAccess::mouseButtonEventCount(const BrowserView *view)
{
    return view && view->m_impl ? view->m_impl->mouseButtonEventCount : 0;
}
