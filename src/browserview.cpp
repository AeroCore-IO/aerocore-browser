#include "browserview.h"
#include "browserview_test_access.h"

#include "browserinpututils.h"
#include "cursorutils.h"

#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QFocusEvent>
#include <QGuiApplication>
#include <QHash>
#include <QHoverEvent>
#include <QImage>
#include <QInputMethodEvent>
#include <QInputMethod>
#include <QKeyEvent>
#include <QMetaObject>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QPainter>
#include <QPointer>
#include <QQuickWindow>
#include <QRegion>
#include <QScreen>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QWheelEvent>

#include <cmath>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#include <include/base/cef_callback.h>
#include <include/cef_browser.h>
#include <include/cef_client.h>
#include <include/cef_request_handler.h>
#include <include/cef_request_context.h>
#include <include/cef_values.h>

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {
constexpr char kVideoPlaybackProcessMessage[] = "AeroCore.VideoPlayback";
constexpr char kGpuDiagnosticsProcessMessage[] = "AeroCore.GpuDiagnostics";

double zoomFactorToLevel(double factor)
{
    const double bounded = std::clamp(factor, 0.8, 3.0);
    return std::log(bounded) / std::log(1.2);
}

double zoomLevelToFactor(double level)
{
    return std::pow(1.2, level);
}

bool zoomBackDebugEnabled()
{
    return qEnvironmentVariableIsSet("AEROCORE_DEBUG_ZOOM_BACK");
}

void logZoomBack(const char *where,
                 BrowserView *view,
                 CefRefPtr<CefBrowser> browser,
                 const QString &extra = QString())
{
    if (!zoomBackDebugEnabled() || !view) {
        return;
    }

    double cefLevel = std::numeric_limits<double>::quiet_NaN();
    if (browser) {
        cefLevel = browser->GetHost()->GetZoomLevel();
    }

    const QSize itemSize = view->size().toSize();
    qInfo().noquote() << QStringLiteral(
        "[DEBUG-a4f2] %1 desiredZoom=%2 cefLevel=%3 cefFactor≈%4 item=%5x%6 scale=%7 uiSf=%8%9")
                             .arg(QString::fromUtf8(where))
                             .arg(view->zoomFactor(), 0, 'f', 3)
                             .arg(cefLevel, 0, 'f', 3)
                             .arg(zoomLevelToFactor(cefLevel), 0, 'f', 3)
                             .arg(itemSize.width())
                             .arg(itemSize.height())
                             .arg(view->scale(), 0, 'f', 3)
                             .arg(view->uiScaleFactor(), 0, 'f', 3)
                             .arg(extra.isEmpty() ? QString() : QStringLiteral(" ") + extra);
}

void applyWebKitFontSizePreferences(const CefRefPtr<CefRequestContext> &requestContext,
                                    int minimumFontSize)
{
    if (!requestContext) {
        return;
    }

    const int boundedMinimum = std::clamp(minimumFontSize, 12, 28);
    const int defaultFontSize = std::max(18, boundedMinimum + 2);

    const auto applyIntPreference = [&requestContext](const char *name, int value) {
        CefString error;
        CefRefPtr<CefValue> preferenceValue = CefValue::Create();
        preferenceValue->SetInt(value);
        // Always log failures (including empty error) — silent no-ops made min-font
        // adjustments look like they worked in UI while CEF kept the old size.
        if (!requestContext->SetPreference(name, preferenceValue, error)) {
            qWarning().noquote() << QStringLiteral("Failed to set browser preference %1: %2")
                                        .arg(QString::fromUtf8(name),
                                             error.empty() ? QStringLiteral("(no error detail)")
                                                           : QString::fromStdString(error.ToString()));
        }
    };

    // PrefService values — CefBrowserSettings.minimum_font_size must stay 0 or CEF
    // permanently overrides these on every renderer prefs push.
    applyIntPreference("webkit.webprefs.minimum_font_size", boundedMinimum);
    applyIntPreference("webkit.webprefs.minimum_logical_font_size", boundedMinimum);
    applyIntPreference("webkit.webprefs.default_font_size", defaultFontSize);
}

CefBrowserHost::MouseButtonType qtMouseButtonToCefButton(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return MBT_LEFT;
    case Qt::MiddleButton:
        return MBT_MIDDLE;
    case Qt::RightButton:
        return MBT_RIGHT;
    default:
        return MBT_LEFT;
    }
}

int qtMouseButtonsToCefModifiers(Qt::MouseButtons buttons)
{
    int modifiers = EVENTFLAG_NONE;
    if (buttons.testFlag(Qt::LeftButton)) {
        modifiers |= EVENTFLAG_LEFT_MOUSE_BUTTON;
    }
    if (buttons.testFlag(Qt::MiddleButton)) {
        modifiers |= EVENTFLAG_MIDDLE_MOUSE_BUTTON;
    }
    if (buttons.testFlag(Qt::RightButton)) {
        modifiers |= EVENTFLAG_RIGHT_MOUSE_BUTTON;
    }
    return modifiers;
}

QString stopMediaPlaybackScript()
{
        return QStringLiteral(R"JS(
(() => {
    for (const element of document.querySelectorAll('video, audio')) {
        try {
            element.pause();
            element.muted = true;
            element.autoplay = false;
            if ('srcObject' in element) {
                element.srcObject = null;
            }
        } catch (e) {
        }
    }
})();
)JS");
}

QString directScrollScript(const QPoint &delta, const QPoint &viewportPos)
{
    return QStringLiteral(R"JS(
(() => {
    const deltaX = %1;
    const deltaY = %2;
    const viewX = %3;
    const viewY = %4;
    const root = document.scrollingElement || document.documentElement || document.body;

    const isScrollable = (element, axis) => {
        if (!element) {
            return false;
        }

        if (element === root) {
            return axis === 'x'
                ? (document.documentElement.scrollWidth > window.innerWidth + 1)
                : (document.documentElement.scrollHeight > window.innerHeight + 1);
        }

        const style = getComputedStyle(element);
        const overflow = axis === 'x' ? style.overflowX : style.overflowY;
        if (!/(auto|scroll|overlay)/.test(overflow)) {
            return false;
        }

        return axis === 'x'
            ? (element.scrollWidth > element.clientWidth + 1)
            : (element.scrollHeight > element.clientHeight + 1);
    };

    const canScrollFurther = (element, delta, axis) => {
        if (!delta) {
            return false;
        }

        const position = element === root
            ? (axis === 'x' ? window.scrollX : window.scrollY)
            : (axis === 'x' ? element.scrollLeft : element.scrollTop);
        const clientSize = element === root
            ? (axis === 'x' ? window.innerWidth : window.innerHeight)
            : (axis === 'x' ? element.clientWidth : element.clientHeight);
        const scrollSize = element === root
            ? (axis === 'x' ? document.documentElement.scrollWidth : document.documentElement.scrollHeight)
            : (axis === 'x' ? element.scrollWidth : element.scrollHeight);

        if (delta < 0) {
            return position > 0;
        }

        return position + clientSize < scrollSize - 1;
    };

    const applyScroll = (element) => {
        const beforeX = element === root ? window.scrollX : element.scrollLeft;
        const beforeY = element === root ? window.scrollY : element.scrollTop;

        if (element === root) {
            window.scrollBy(deltaX, deltaY);
        } else {
            element.scrollBy({
                left: deltaX,
                top: deltaY,
                behavior: 'instant'
            });
        }

        const afterX = element === root ? window.scrollX : element.scrollLeft;
        const afterY = element === root ? window.scrollY : element.scrollTop;
        return beforeX !== afterX || beforeY !== afterY;
    };

    const wantsScroll = (element) => {
        return (deltaX !== 0 && isScrollable(element, 'x') && canScrollFurther(element, deltaX, 'x'))
            || (deltaY !== 0 && isScrollable(element, 'y') && canScrollFurther(element, deltaY, 'y'));
    };

    const visited = new Set();
    const tryCandidate = (element) => {
        if (!element || visited.has(element)) {
            return false;
        }

        visited.add(element);
        return wantsScroll(element) && applyScroll(element);
    };

    const hitStack = typeof document.elementsFromPoint === 'function'
        ? document.elementsFromPoint(viewX, viewY)
        : [document.elementFromPoint(viewX, viewY)].filter(Boolean);

    for (const hit of hitStack) {
        if (tryCandidate(hit)) {
            return;
        }
    }

    for (const hit of hitStack) {
        let candidate = hit ? hit.parentElement : null;
        while (candidate) {
            if (tryCandidate(candidate)) {
                return;
            }
            candidate = candidate.parentElement;
        }
    }

    const coveringCandidates = Array.from(document.querySelectorAll('*'))
        .filter((element) => {
            if (visited.has(element) || !wantsScroll(element)) {
                return false;
            }

            const rect = element.getBoundingClientRect();
            return rect.width > 0 && rect.height > 0
                && viewX >= rect.left && viewX <= rect.right
                && viewY >= rect.top && viewY <= rect.bottom;
        })
        .sort((left, right) => {
            const leftRect = left.getBoundingClientRect();
            const rightRect = right.getBoundingClientRect();
            const leftArea = leftRect.width * leftRect.height;
            const rightArea = rightRect.width * rightRect.height;
            return leftArea - rightArea;
        });

    for (const element of coveringCandidates) {
        if (applyScroll(element)) {
            return;
        }
    }

    const primaryViewportCandidates = Array.from(document.querySelectorAll('*'))
        .filter((element) => {
            if (visited.has(element) || !wantsScroll(element)) {
                return false;
            }

            const rect = element.getBoundingClientRect();
            if (rect.width <= 0 || rect.height <= 0) {
                return false;
            }

            const overlapWidth = Math.max(0, Math.min(rect.right, window.innerWidth) - Math.max(rect.left, 0));
            const overlapHeight = Math.max(0, Math.min(rect.bottom, window.innerHeight) - Math.max(rect.top, 0));
            return overlapWidth > 0 && overlapHeight > 0;
        })
        .sort((left, right) => {
            const leftRect = left.getBoundingClientRect();
            const rightRect = right.getBoundingClientRect();
            const leftOverlap = Math.max(0, Math.min(leftRect.right, window.innerWidth) - Math.max(leftRect.left, 0))
                * Math.max(0, Math.min(leftRect.bottom, window.innerHeight) - Math.max(leftRect.top, 0));
            const rightOverlap = Math.max(0, Math.min(rightRect.right, window.innerWidth) - Math.max(rightRect.left, 0))
                * Math.max(0, Math.min(rightRect.bottom, window.innerHeight) - Math.max(rightRect.top, 0));
            if (leftOverlap !== rightOverlap) {
                return rightOverlap - leftOverlap;
            }

            const leftArea = leftRect.width * leftRect.height;
            const rightArea = rightRect.width * rightRect.height;
            return leftArea - rightArea;
        });

    for (const element of primaryViewportCandidates) {
        if (applyScroll(element)) {
            return;
        }
    }

    if (root) {
        applyScroll(root);
    }
})();
)JS")
        .arg(delta.x())
        .arg(delta.y())
        .arg(viewportPos.x())
        .arg(viewportPos.y());
}

template <typename Callback>
void queueForView(BrowserView *view, Callback &&callback)
{
    QPointer<BrowserView> guardedView(view);
    if (!guardedView) {
        return;
    }

    QMetaObject::invokeMethod(view,
                              [guardedView, callback = std::forward<Callback>(callback)]() mutable {
                                  if (!guardedView) {
                                      return;
                                  }
                                  callback(guardedView.data());
                              },
                              Qt::QueuedConnection);
}

QString terminationStatusToString(CefRequestHandler::TerminationStatus status)
{
    switch (status) {
    case TS_ABNORMAL_TERMINATION:
        return QStringLiteral("abnormal-exit");
    case TS_PROCESS_WAS_KILLED:
        return QStringLiteral("killed");
    case TS_PROCESS_CRASHED:
        return QStringLiteral("crashed");
    case TS_PROCESS_OOM:
        return QStringLiteral("oom");
    case TS_LAUNCH_FAILED:
        return QStringLiteral("launch-failed");
    case TS_INTEGRITY_FAILURE:
        return QStringLiteral("integrity-failure");
    case TS_NUM_VALUES:
        break;
    }

    return QStringLiteral("unknown");
}

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
Display *browserCursorDisplay()
{
    return sharedX11Display();
}

void setWindowCursorVisible(Window window, bool visible)
{
    Display *display = browserCursorDisplay();
    if (!display || window == 0) {
        return;
    }

    static ::Cursor blankCursor = 0;
    if (blankCursor == 0) {
        char data[1] = {0};
        Pixmap pixmap = XCreateBitmapFromData(display, DefaultRootWindow(display), data, 1, 1);
        if (pixmap) {
            XColor color{};
            blankCursor = XCreatePixmapCursor(display, pixmap, pixmap, &color, &color, 0, 0);
            XFreePixmap(display, pixmap);
        }
    }

    if (visible) {
        XUndefineCursor(display, window);
    } else if (blankCursor != 0) {
        XDefineCursor(display, window, blankCursor);
    } else {
        XUndefineCursor(display, window);
    }
    XFlush(display);
}

void requestWindowInputFocus(Window window)
{
    Display *display = browserCursorDisplay();
    if (!display || window == 0) {
        return;
    }

    XRaiseWindow(display, window);
    XSetInputFocus(display, window, RevertToPointerRoot, CurrentTime);
    XFlush(display);
}

void setAtomListProperty(Display *display, Window window, const char *propertyName, const std::initializer_list<const char *> &atomNames)
{
    if (!display || window == 0) {
        return;
    }

    const Atom property = XInternAtom(display, propertyName, False);
    if (property == None) {
        return;
    }

    std::vector<Atom> atoms;
    atoms.reserve(atomNames.size());
    for (const char *atomName : atomNames) {
        const Atom atom = XInternAtom(display, atomName, False);
        if (atom != None) {
            atoms.push_back(atom);
        }
    }

    if (atoms.empty()) {
        return;
    }

    XChangeProperty(display,
                    window,
                    property,
                    XA_ATOM,
                    32,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char *>(atoms.data()),
                    int(atoms.size()));
}

void setUtf8WindowProperty(Display *display, Window window, const char *propertyName, const QString &value)
{
    if (!display || window == 0) {
        return;
    }

    const Atom property = XInternAtom(display, propertyName, False);
    const Atom utf8String = XInternAtom(display, "UTF8_STRING", False);
    if (property == None || utf8String == None) {
        return;
    }

    const QByteArray encoded = value.toUtf8();
    XChangeProperty(display,
                    window,
                    property,
                    utf8String,
                    8,
                    PropModeReplace,
                    reinterpret_cast<const unsigned char *>(encoded.constData()),
                    int(encoded.size()));
}

void configureEmbeddedBrowserWindow(Window browserWindow, Window transientParent)
{
    Display *display = browserCursorDisplay();
    if (!display || browserWindow == 0) {
        return;
    }

    if (transientParent != 0) {
        XSetTransientForHint(display, browserWindow, transientParent);
    }

    setAtomListProperty(display,
                        browserWindow,
                        "_NET_WM_STATE",
                        {"_NET_WM_STATE_SKIP_TASKBAR", "_NET_WM_STATE_SKIP_PAGER"});
    setAtomListProperty(display,
                        browserWindow,
                        "_NET_WM_WINDOW_TYPE",
                        {"_NET_WM_WINDOW_TYPE_UTILITY"});
    setUtf8WindowProperty(display, browserWindow, "_NET_WM_NAME", QStringLiteral("AeroCore Embedded"));
    XStoreName(display, browserWindow, "AeroCore Embedded");
    XFlush(display);
}
#endif

}

struct BrowserView::Impl {
    // Empty until loadUrl()/session restore supplies a real target. A hard-coded
    // default caused every new tab to CreateBrowserSync(bilibili) before the
    // intended loadUrl — double navigation and multi-second first-frame stalls.
    QString pendingUrl;
    QString currentUrl;
    QString currentTitle;
    bool canGoBack = false;
    bool canGoForward = false;
    bool isLoading = false;
    bool browserClosing = false;
    bool fullscreenMode = false;
    bool audiblePlaybackActive = false;
    QSet<QString> activeVideoFrameIds;
    QSet<QString> pictureInPictureFrameIds;
    QSet<QString> fullscreenVideoFrameIds;
    QHash<QString, quint8> lastVideoPlaybackStateByFrame;
    QString lastGpuDiagnosticsSignature;
    double zoomFactor = 1.2;
    int minimumFontSize = 20;
    qreal deviceScaleFactor = 1.0;
    qreal uiScaleFactor = 1.0;
    Qt::MouseButtons pressedMouseButtons = Qt::NoButton;
    // Qt Quick may resend HoverMove on every frame while any item is dirty, even when the pointer
    // is stationary. OSR video paints keep this item dirty at about 60 fps, preventing player
    // controls from auto-hiding. Deduplicate by pointer position and button state; real movement
    // changes the position and is still delivered.
    QPoint lastSentCefMousePos;
    Qt::MouseButtons lastSentCefMouseButtons = Qt::NoButton;
    bool lastSentCefMouseValid = false;
    bool selectionDragActive = false;
    bool audioMuted = false;
    bool tabActive = false;
    // After loadUrl/reload, emit firstFrameAvailable on the next OSR paint even
    // if a cached frame already exists — so the loading spinner can dismiss as
    // soon as fresh pixels arrive without blanking the old frame first.
    bool notifyNextFrame = false;
    // AEROCORE_DEBUG_ZOOM_BACK: remaining OSR paint samples to log after goBack.
    int zoomBackPaintSamplesRemaining = 0;
    // paint() reads these buffers on the scene-graph thread while handleOsrPaint() writes them on
    // the main thread. QQuickPaintedItem needs a lock to prevent drawing a partially copied frame.
    mutable QMutex frameMutex;
    QImage viewImage;
    QImage popupImage;
    QRect popupRect;
    bool popupVisible = false;
    QRectF imeCursorRect;
    int winIdRetryCount = 0;
    QMetaObject::Connection windowChangedConnection;
    QMetaObject::Connection windowSceneGraphConnection;
    CefRefPtr<CefBrowser> browser;
    CefRefPtr<CefClient> client;

    // Frame watchdog: detects OSR renderer stalls (no frames despite visible
    // browser) and recovers via Invalidate escalation then full re-navigation.
    QTimer frameWatchdogTimer;
    qint64 watchdogLastFrameTime = 0;    // ms since epoch of last OnPaint
    qint64 watchdogLastPokeTime = 0;     // ms since epoch of last Invalidate poke
    qint64 watchdogLastHeartbeat = 0;    // for suspend gap detection
    int watchdogStallChecks = 0;         // consecutive checks without a frame
    int watchdogRecoveryAttempts = 0;    // reload attempts since last healthy frame
};

class BrowserViewClient final
    : public CefClient
    , public CefDisplayHandler
    , public CefLifeSpanHandler
    , public CefLoadHandler
    , public CefRequestHandler
    , public CefRenderHandler
{
public:
    explicit BrowserViewClient(BrowserView *view)
        : m_view(view)
    {
    }

    void detachView()
    {
        m_view.clear();
    }

    CefRefPtr<CefDisplayHandler> GetDisplayHandler() override { return this; }
    CefRefPtr<CefLifeSpanHandler> GetLifeSpanHandler() override { return this; }
    CefRefPtr<CefLoadHandler> GetLoadHandler() override { return this; }
    CefRefPtr<CefRequestHandler> GetRequestHandler() override { return this; }
    CefRefPtr<CefRenderHandler> GetRenderHandler() override { return this; }

    void OnAfterCreated(CefRefPtr<CefBrowser> browser) override
    {
        qInfo() << "CEF OnAfterCreated browserId=" << (browser ? browser->GetIdentifier() : -1)
                << "client=" << static_cast<const void *>(this)
                << "view=" << static_cast<const void *>(m_view.data());
        queueForView(m_view, [](BrowserView *view) {
            view->refreshNativeBrowserWindowHints();
            view->requestNativeFocus();
            view->resizeBrowserToView();
        });
    }

    void OnBeforeClose(CefRefPtr<CefBrowser>) override
    {
        queueForView(m_view, [](BrowserView *view) {
            view->handleBrowserClosed();
        });
    }

    void requestNewTab(const QString &url)
    {
        if (url.isEmpty() || url.compare(QStringLiteral("about:blank"), Qt::CaseInsensitive) == 0) {
            return;
        }

        queueForView(m_view, [url](BrowserView *view) {
            emit view->newTabRequested(url);
        });
    }

    bool OnBeforePopup(CefRefPtr<CefBrowser> browser,
                       CefRefPtr<CefFrame>,
                       int,
                       const CefString &targetUrl,
                       const CefString &,
                       CefLifeSpanHandler::WindowOpenDisposition,
                       bool,
                       const CefPopupFeatures &,
                       CefWindowInfo &,
                       CefRefPtr<CefClient> &,
                       CefBrowserSettings &,
                       CefRefPtr<CefDictionaryValue> &,
                       bool *) override
    {
        Q_UNUSED(browser);
                    requestNewTab(QString::fromStdString(targetUrl.ToString()).trimmed());
        return true;
    }

    bool OnOpenURLFromTab(CefRefPtr<CefBrowser>,
                          CefRefPtr<CefFrame>,
                          const CefString &targetUrl,
                          CefRequestHandler::WindowOpenDisposition,
                          bool) override
    {
        requestNewTab(QString::fromStdString(targetUrl.ToString()).trimmed());
        return true;
    }

    void OnAddressChange(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, const CefString &url) override
    {
        if (!frame->IsMain()) {
            return;
        }

        const int browserId = browser ? browser->GetIdentifier() : -1;
        const QString nextUrl = QString::fromStdString(url.ToString());
        // Capture CEF zoom on the CEF thread before queueing — distinguishes
        // "CEF already reset zoom" from "we re-applied later on Qt thread".
        const double cefLevelAtChange = browser ? browser->GetHost()->GetZoomLevel() : 0.0;
        queueForView(m_view, [browserId, nextUrl, cefLevelAtChange](BrowserView *view) {
            if (!view->m_impl->browser || view->m_impl->browserClosing || view->m_impl->browser->GetIdentifier() != browserId) {
                return;
            }
            logZoomBack("OnAddressChange", view, view->m_impl->browser,
                        QStringLiteral("url=%1 cefLevelAtCallback=%2")
                            .arg(nextUrl, QString::number(cefLevelAtChange, 'f', 3)));
            emit view->addressChanged(nextUrl);
        });
    }

    void OnTitleChange(CefRefPtr<CefBrowser> browser, const CefString &title) override
    {
        const int browserId = browser ? browser->GetIdentifier() : -1;
        const QString nextTitle = QString::fromStdString(title.ToString());
        queueForView(m_view, [browserId, nextTitle](BrowserView *view) {
            if (!view->m_impl->browser || view->m_impl->browserClosing || view->m_impl->browser->GetIdentifier() != browserId) {
                return;
            }
            emit view->titleChanged(nextTitle);
        });
    }

    void OnLoadingStateChange(CefRefPtr<CefBrowser> browser, bool isLoading, bool canGoBack, bool canGoForward) override
    {
        const int browserId = browser ? browser->GetIdentifier() : -1;
        const double cefLevel = browser ? browser->GetHost()->GetZoomLevel() : 0.0;
        queueForView(m_view, [browserId, isLoading, canGoBack, canGoForward, cefLevel](BrowserView *view) {
            if (!view->m_impl->browser || view->m_impl->browserClosing || view->m_impl->browser->GetIdentifier() != browserId) {
                return;
            }
            if (zoomBackDebugEnabled()) {
                logZoomBack("OnLoadingStateChange", view, view->m_impl->browser,
                            QStringLiteral("isLoading=%1 cefLevelAtCallback=%2")
                                .arg(isLoading ? 1 : 0)
                                .arg(cefLevel, 0, 'f', 3));
            }
            if (view->m_impl->isLoading != isLoading) {
                view->m_impl->isLoading = isLoading;
                emit view->loadingStateChanged(isLoading);
            }
            emit view->navigationStateChanged(canGoBack, canGoForward);
        });
    }

    void OnLoadStart(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, TransitionType) override
    {
        const int browserId = browser ? browser->GetIdentifier() : -1;
        const QString frameId = frame ? QString::fromStdString(frame->GetIdentifier().ToString()) : QString();
        const double cefLevel = browser ? browser->GetHost()->GetZoomLevel() : 0.0;
        const bool isMain = frame && frame->IsMain();
        queueForView(m_view, [browserId, frameId, cefLevel, isMain](BrowserView *view) {
            if (!view->m_impl->browser || view->m_impl->browserClosing || view->m_impl->browser->GetIdentifier() != browserId) {
                return;
            }
            if (isMain) {
                logZoomBack("OnLoadStart(main)", view, view->m_impl->browser,
                            QStringLiteral("cefLevelAtCallback=%1").arg(cefLevel, 0, 'f', 3));
            }
            view->setVideoPlaybackState(frameId, false, false, false);
        });
    }

    void OnLoadEnd(CefRefPtr<CefBrowser> browser, CefRefPtr<CefFrame> frame, int) override
    {
        if (!frame->IsMain()) {
            return;
        }
        const int browserId = browser ? browser->GetIdentifier() : -1;
        const QString url = QString::fromStdString(frame->GetURL().ToString());
        queueForView(m_view, [browserId, url](BrowserView *view) {
            if (!view->m_impl->browser || view->m_impl->browserClosing || view->m_impl->browser->GetIdentifier() != browserId) {
                return;
            }
            emit view->loadSucceeded(url, view->currentTitle());
        });
    }

    void OnLoadError(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, ErrorCode errorCode, const CefString &errorText, const CefString &failedUrl) override
    {
        if (!frame->IsMain() || errorCode == ERR_ABORTED) {
            return;
        }

        const QString url = QString::fromStdString(failedUrl.ToString());
        const QString message = QString::fromStdString(errorText.ToString());
        queueForView(m_view, [url, message, errorCode](BrowserView *view) {
            emit view->loadFailed(url, int(errorCode), message);
        });
    }

    void OnFullscreenModeChange(CefRefPtr<CefBrowser>, bool fullscreen) override
    {
        queueForView(m_view, [fullscreen](BrowserView *view) {
            view->m_impl->fullscreenMode = fullscreen;
            emit view->fullscreenModeChanged(fullscreen);
        });
    }

    bool OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                  CefRefPtr<CefFrame> frame,
                                  CefProcessId source_process,
                                  CefRefPtr<CefProcessMessage> message) override
    {
        if (source_process != PID_RENDERER || !message) {
            return false;
        }

        const QString messageName = QString::fromStdString(message->GetName().ToString());

        if (messageName == QString::fromLatin1(kGpuDiagnosticsProcessMessage)) {
            const int browserId = browser ? browser->GetIdentifier() : -1;
            const QString frameUrl = frame ? QString::fromStdString(frame->GetURL().ToString()) : QString();
            const QString backend = message->GetArgumentList() && message->GetArgumentList()->GetSize() > 0
                ? QString::fromStdString(message->GetArgumentList()->GetString(0).ToString())
                : QString();
            const QString vendor = message->GetArgumentList() && message->GetArgumentList()->GetSize() > 1
                ? QString::fromStdString(message->GetArgumentList()->GetString(1).ToString())
                : QString();
            const QString renderer = message->GetArgumentList() && message->GetArgumentList()->GetSize() > 2
                ? QString::fromStdString(message->GetArgumentList()->GetString(2).ToString())
                : QString();

            queueForView(m_view, [browserId, frameUrl, backend, vendor, renderer](BrowserView *view) {
                if (!view->m_impl->browser || view->m_impl->browserClosing || view->m_impl->browser->GetIdentifier() != browserId) {
                    return;
                }

                const QString signature = backend + QLatin1Char('\n') + vendor + QLatin1Char('\n') + renderer;
                if (view->m_impl->lastGpuDiagnosticsSignature == signature) {
                    return;
                }
                view->m_impl->lastGpuDiagnosticsSignature = signature;

                qInfo().noquote() << QStringLiteral("CEF GPU diagnostics: backend=%1 vendor=%2 renderer=%3 url=%4")
                                         .arg(backend.isEmpty() ? QStringLiteral("<unknown>") : backend,
                                              vendor.isEmpty() ? QStringLiteral("<unknown>") : vendor,
                                              renderer.isEmpty() ? QStringLiteral("<unknown>") : renderer,
                                              frameUrl.isEmpty() ? QStringLiteral("<unknown>") : frameUrl);
            });
            return true;
        }

        if (messageName != QString::fromLatin1(kVideoPlaybackProcessMessage)) {
            return false;
        }

        const int browserId = browser ? browser->GetIdentifier() : -1;
        const QString frameId = frame ? QString::fromStdString(frame->GetIdentifier().ToString()) : QString();
        const bool videoActive = message->GetArgumentList() && message->GetArgumentList()->GetSize() > 0
            ? message->GetArgumentList()->GetBool(0)
            : false;
        const bool pictureInPictureActive = message->GetArgumentList() && message->GetArgumentList()->GetSize() > 1
            ? message->GetArgumentList()->GetBool(1)
            : false;
        const bool fullscreenVideoActive = message->GetArgumentList() && message->GetArgumentList()->GetSize() > 2
            ? message->GetArgumentList()->GetBool(2)
            : false;
        queueForView(m_view, [browserId, frameId, videoActive, pictureInPictureActive, fullscreenVideoActive](BrowserView *view) {
            if (!view->m_impl->browser || view->m_impl->browserClosing || view->m_impl->browser->GetIdentifier() != browserId) {
                return;
            }
            view->setVideoPlaybackState(frameId, videoActive, pictureInPictureActive, fullscreenVideoActive);
        });
        return true;
    }

    void OnRenderProcessTerminated(CefRefPtr<CefBrowser> browser,
                                   TerminationStatus status,
                                   int errorCode,
                                   const CefString &errorString) override
    {
        const int browserId = browser ? browser->GetIdentifier() : -1;
        const QString statusText = terminationStatusToString(status);
        const QString message = QString::fromStdString(errorString.ToString()).trimmed();
        qWarning().noquote()
            << QStringLiteral("CEF renderer terminated: browserId=%1 status=%2 errorCode=%3 error=%4")
                   .arg(browserId)
                   .arg(statusText)
                   .arg(errorCode)
                   .arg(message.isEmpty() ? QStringLiteral("<none>") : message);

        queueForView(m_view, [browserId, status, errorCode, message](BrowserView *view) {
            if (!view->m_impl->browser || view->m_impl->browser->GetIdentifier() != browserId) {
                return;
            }
            emit view->renderProcessTerminated(int(status), errorCode, message);
        });
    }

    void GetViewRect(CefRefPtr<CefBrowser>, CefRect &rect) override
    {
        rect = m_view ? m_view->viewRectForOsr() : CefRect(0, 0, 1, 1);
    }

    bool GetScreenPoint(CefRefPtr<CefBrowser>, int viewX, int viewY, int &screenX, int &screenY) override
    {
        return m_view && m_view->mapPointToScreenForOsr(viewX, viewY, screenX, screenY);
    }

    bool GetScreenInfo(CefRefPtr<CefBrowser>, CefScreenInfo &screenInfo) override
    {
        return m_view && m_view->fillScreenInfoForOsr(screenInfo);
    }

    void OnPopupShow(CefRefPtr<CefBrowser>, bool show) override
    {
        queueForView(m_view, [show](BrowserView *view) {
            view->handlePopupVisibility(show);
        });
    }

    void OnPopupSize(CefRefPtr<CefBrowser>, const CefRect &rect) override
    {
        queueForView(m_view, [rect](BrowserView *view) {
            view->handlePopupRectChanged(rect);
        });
    }

    void OnImeCompositionRangeChanged(CefRefPtr<CefBrowser>,
                                      const CefRange &,
                                      const RectList &characterBounds) override
    {
        RectList boundsCopy = characterBounds;
        queueForView(m_view, [boundsCopy = std::move(boundsCopy)](BrowserView *view) {
            view->handleImeCompositionRangeChanged(boundsCopy);
        });
    }

    // The renderer sends this when an editable element gains or loses focus. kNone hides the
    // virtual keyboard; this lets gamepad activation of a web input open the Steam OSK.
    void OnVirtualKeyboardRequested(CefRefPtr<CefBrowser> browser,
                                    TextInputMode inputMode) override
    {
        const int browserId = browser ? browser->GetIdentifier() : -1;
        const bool requested = inputMode != CEF_TEXT_INPUT_MODE_NONE;
        qInfo() << "CEF OnVirtualKeyboardRequested browserId=" << browserId
                << "inputMode=" << static_cast<int>(inputMode)
                << "client=" << static_cast<const void *>(this)
                << "view=" << static_cast<const void *>(m_view.data());
        queueForView(m_view, [browserId, requested](BrowserView *view) {
            const bool browserNull = !view->m_impl->browser;
            const bool closing = view->m_impl->browserClosing;
            const int currentId = view->m_impl->browser ? view->m_impl->browser->GetIdentifier() : -1;
            // This callback carries only browserId and a bool, with no CEF object references.
            // queueForView's QPointer protects the view lifetime, so discard only when the browser
            // is closing. Rejecting a temporarily mismatched ID could prevent the keyboard session
            // from being established.
            if (browserNull || closing) {
                qWarning() << "CEF OnVirtualKeyboardRequested dropped: browserId=" << browserId
                           << "browserNull=" << browserNull << "closing=" << closing
                           << "currentId=" << currentId
                           << "view=" << static_cast<const void *>(view);
                return;
            }
            if (currentId != browserId) {
                qInfo() << "CEF OnVirtualKeyboardRequested id drift: event=" << browserId
                        << "current=" << currentId;
            }
            emit view->virtualKeyboardRequested(requested);
        });
    }

    void OnPaint(CefRefPtr<CefBrowser>,
                 PaintElementType type,
                 const RectList &dirtyRects,
                 const void *buffer,
                 int width,
                 int height) override
    {
        if (!m_view || !buffer || width <= 0 || height <= 0) {
            return;
        }

        QImage wrapped(static_cast<const uchar *>(buffer), width, height, width * 4, QImage::Format_ARGB32);
        if (QThread::currentThread() == m_view->thread()) {
            m_view->handleOsrPaint(type, dirtyRects, wrapped);
            return;
        }

        QImage owned = wrapped.copy();
        RectList dirtyCopy = dirtyRects;
        queueForView(m_view, [type, dirtyCopy = std::move(dirtyCopy), owned = std::move(owned)](BrowserView *view) mutable {
            view->handleOsrPaint(type, dirtyCopy, owned);
        });
    }

private:
    QPointer<BrowserView> m_view;

    IMPLEMENT_REFCOUNTING(BrowserViewClient);
};

BrowserView::BrowserView(QQuickItem *parent)
    : QQuickPaintedItem(parent)
    , m_impl(std::make_unique<Impl>())
{
    setRenderTarget(QQuickPaintedItem::Image);
    setOpaquePainting(true);
    setAntialiasing(false);
    setAcceptedMouseButtons(Qt::AllButtons);
    setAcceptHoverEvents(true);
    setAcceptTouchEvents(false);
    setFlag(QQuickItem::ItemIsFocusScope, true);
    setFlag(QQuickItem::ItemAcceptsInputMethod, true);
    setFocus(false);
#if QT_VERSION >= QT_VERSION_CHECK(6, 7, 0)
    setCursor(QCursor(Qt::BlankCursor));
#endif

    m_impl->frameWatchdogTimer.setInterval(3000);
    connect(&m_impl->frameWatchdogTimer, &QTimer::timeout,
            this, &BrowserView::onFrameWatchdogTimeout);

    connect(this, &BrowserView::addressChanged, this, &BrowserView::handleAddressSignal);
    connect(this, &BrowserView::titleChanged, this, &BrowserView::handleTitleSignal);
    connect(this, &BrowserView::navigationStateChanged, this, &BrowserView::handleNavigationSignal);
    // Must disconnect in ~BrowserView before m_impl is destroyed: ~QQuickItem emits
    // windowChanged while tearing down parent/window, after subclass members are gone.
    m_impl->windowChangedConnection =
        connect(this, &QQuickItem::windowChanged, this, [this](QQuickWindow *quickWindow) {
            bindQuickWindow(quickWindow);
        });
    bindQuickWindow(window());
}

BrowserView::~BrowserView()
{
    m_impl->frameWatchdogTimer.stop();

    if (m_impl->windowChangedConnection) {
        disconnect(m_impl->windowChangedConnection);
        m_impl->windowChangedConnection = {};
    }
    bindQuickWindow(nullptr);

    if (m_impl->client) {
        static_cast<BrowserViewClient *>(m_impl->client.get())->detachView();
    }

    if (m_impl->browser && !m_impl->browserClosing) {
        m_impl->browserClosing = true;
        m_impl->browser->GetHost()->CloseBrowser(true);
    }

    m_impl->browser = nullptr;
    m_impl->client = nullptr;
}

void BrowserView::setBrowserInputEnabled(bool enabled)
{
    setAcceptedMouseButtons(enabled ? Qt::AllButtons : Qt::MouseButtons());
    setAcceptHoverEvents(enabled);
    setFlag(QQuickItem::ItemAcceptsInputMethod, enabled);
    if (!enabled) {
        setFocus(false);
    }
}

void BrowserView::setUiScaleFactor(qreal factor)
{
    const qreal bounded = std::max<qreal>(factor, 0.01);
    if (qFuzzyCompare(m_impl->uiScaleFactor, bounded)) {
        return;
    }

    m_impl->uiScaleFactor = bounded;
    resizeBrowserToView();
    update();
}

qreal BrowserView::uiScaleFactor() const
{
    return m_impl->uiScaleFactor;
}

qreal BrowserView::effectiveUiScale() const
{
    return std::max<qreal>(m_impl->uiScaleFactor, 0.01);
}

bool BrowserView::isViewActive() const
{
    // Explicit tab-active flag — deliberately NOT QQuickItem::isVisible(),
    // which reflects *effective* visibility including ancestor items.
    // Browser liveness must follow tab-active state, not page-level
    // visibility: hiding the whole page must not stop the OSR compositor.
    // This matches the QWidget master branch, which never propagated page
    // hides to the views.
    return m_impl->tabActive;
}

void BrowserView::setTabActive(bool active)
{
    if (m_impl->tabActive == active) {
        return;
    }
    m_impl->tabActive = active;
    qInfo().noquote() << QStringLiteral("[osr] tab %1 (browser=%2)")
                             .arg(active ? QStringLiteral("active") : QStringLiteral("inactive"))
                             .arg(m_impl->browser ? m_impl->browser->GetIdentifier() : -1);
    if (active) {
        QSize frameLogical;
        qreal frameDpr = 1.0;
        {
            QMutexLocker locker(&m_impl->frameMutex);
            if (!m_impl->viewImage.isNull()) {
                frameDpr = std::max<qreal>(m_impl->viewImage.devicePixelRatio(), 0.01);
                frameLogical = QSize(qRound(m_impl->viewImage.width() / frameDpr),
                                     qRound(m_impl->viewImage.height() / frameDpr));
            }
        }
        const QSize itemSize = cefViewSize();
        if (!frameLogical.isEmpty() && frameLogical != itemSize) {
            qInfo().noquote() << QStringLiteral(
                "[DEBUG-a4f2] tab-activate cache mismatch frameLogical=%1x%2 item=%3x%4 dpr=%5 (close-tab shrink flash)")
                                     .arg(frameLogical.width())
                                     .arg(frameLogical.height())
                                     .arg(itemSize.width())
                                     .arg(itemSize.height())
                                     .arg(frameDpr, 0, 'f', 3);
        }
    }
    applyVisibleState(active);
}

void BrowserView::setAudioMuted(bool muted)
{
    m_impl->audioMuted = muted;
    if (m_impl->browser && !m_impl->browserClosing) {
        m_impl->browser->GetHost()->SetAudioMuted(muted);
    }
}

QSize BrowserView::cefViewSize() const
{
    // BrowserContainer sets the item size in screen logical pixels, matching the ES GetViewRect.
    return QSize(std::max(1, int(std::lround(width()))),
                 std::max(1, int(std::lround(height()))));
}

QPoint BrowserView::itemToCefPoint(const QPoint &itemPos) const
{
    return itemPos;
}

QRectF BrowserView::cefRectToItemRect(const QRect &cefRect) const
{
    return QRectF(cefRect);
}

QPoint BrowserView::mapPointFromGlobal(const QPoint &globalPos) const
{
    return mapFromGlobal(QPointF(globalPos)).toPoint();
}

bool BrowserView::containsGlobalPoint(const QPoint &globalPos) const
{
    if (!isVisible() || width() <= 0.0 || height() <= 0.0) {
        return false;
    }

    const QPoint local = mapPointFromGlobal(globalPos);
    return QRectF(QPointF(0.0, 0.0), QSizeF(width(), height())).contains(local);
}

qreal BrowserView::viewDevicePixelRatio() const
{
    if (const QQuickWindow *quickWindow = window()) {
        return std::max<qreal>(quickWindow->effectiveDevicePixelRatio(), 1.0);
    }
    return 1.0;
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
    if (m_impl->browser && !m_impl->browserClosing) {
        return m_impl->browser->CanGoBack();
    }

    return m_impl->canGoBack;
}

bool BrowserView::canGoForwardNow() const
{
    if (m_impl->browser && !m_impl->browserClosing) {
        return m_impl->browser->CanGoForward();
    }

    return m_impl->canGoForward;
}

bool BrowserView::isLoading() const
{
    return m_impl->isLoading;
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

    QMutexLocker locker(&view->m_impl->frameMutex);
    const bool currentlyHasFrame = !view->m_impl->viewImage.isNull();
    if (currentlyHasFrame == hasFrame) {
        return;
    }

    if (hasFrame) {
        view->m_impl->viewImage = QImage(1, 1, QImage::Format_ARGB32);
        view->m_impl->viewImage.fill(qRgba(0, 0, 0, 255));
    } else {
        view->m_impl->viewImage = QImage();
    }
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

    {
        QMutexLocker locker(&view->m_impl->frameMutex);
        if (!view->m_impl->notifyNextFrame && view->m_impl->viewImage.isNull()) {
            return;
        }
        view->m_impl->notifyNextFrame = false;
        if (view->m_impl->viewImage.isNull()) {
            view->m_impl->viewImage = QImage(1, 1, QImage::Format_ARGB32);
            view->m_impl->viewImage.fill(qRgba(0, 0, 0, 255));
        }
    }
    emit view->firstFrameAvailable();
}

double BrowserView::zoomFactor() const
{
    return m_impl->zoomFactor;
}

bool BrowserView::hasLiveBrowser() const
{
    return m_impl->browser != nullptr;
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
    return m_impl->audiblePlaybackActive;
}

bool BrowserView::isVideoPlaybackActive() const
{
    return !m_impl->activeVideoFrameIds.isEmpty();
}

bool BrowserView::isPictureInPicturePlaybackActive() const
{
    return !m_impl->pictureInPictureFrameIds.isEmpty();
}

bool BrowserView::isFullscreenVideoPlaybackActive() const
{
    return !m_impl->fullscreenVideoFrameIds.isEmpty();
}

bool BrowserView::isMediaPlaybackActive() const
{
    return m_impl->audiblePlaybackActive
        || !m_impl->activeVideoFrameIds.isEmpty()
        || !m_impl->pictureInPictureFrameIds.isEmpty()
        || !m_impl->fullscreenVideoFrameIds.isEmpty();
}

QImage BrowserView::captureViewImage() const
{
    QMutexLocker locker(&m_impl->frameMutex);
    if (m_impl->viewImage.isNull()) {
        return {};
    }
    return m_impl->viewImage.copy();
}

bool BrowserView::hasFrame() const
{
    // True once the OSR renderer has delivered at least one frame. Used by
    // the QML loading spinner to switch to the page as soon as there is
    // anything to show (progressive rendering then continues live), instead
    // of waiting for the whole load to finish.
    QMutexLocker locker(&m_impl->frameMutex);
    return !m_impl->viewImage.isNull();
}

void BrowserView::loadUrl(const QString &url)
{
    if (url.trimmed().isEmpty()) {
        return;
    }

    m_impl->pendingUrl = url.trimmed();
    if (m_impl->browserClosing) {
        return;
    }

    // Keep any cached frame on screen under the spinner; ask the next paint to
    // re-emit firstFrameAvailable so QML can dismiss loading without waiting for
    // a full document load (and without blanking to an empty buffer first).
    m_impl->notifyNextFrame = true;

    const bool hadBrowser = m_impl->browser != nullptr;
    scheduleCreateBrowserIfNeeded();
    // CreateBrowserSync already starts navigation with pendingUrl. Only call
    // LoadURL when the browser already existed (in-tab navigation / reload).
    if (m_impl->browser && hadBrowser) {
        m_impl->browser->GetMainFrame()->LoadURL(m_impl->pendingUrl.toStdString());
        if (qEnvironmentVariableIsSet("AEROCORE_LOAD_TIMING")) {
            qInfo().noquote() << QStringLiteral("[load-timing] BrowserView::loadUrl navigate existing browser url=%1 hasFrame=%2")
                                     .arg(m_impl->pendingUrl)
                                     .arg(hasFrame() ? QStringLiteral("1") : QStringLiteral("0"));
        }
    } else if (qEnvironmentVariableIsSet("AEROCORE_LOAD_TIMING")) {
        qInfo().noquote() << QStringLiteral("[load-timing] BrowserView::loadUrl create-or-pending url=%1 hadBrowser=%2 hasBrowser=%3 hasFrame=%4")
                                 .arg(m_impl->pendingUrl)
                                 .arg(hadBrowser ? QStringLiteral("1") : QStringLiteral("0"))
                                 .arg(m_impl->browser ? QStringLiteral("1") : QStringLiteral("0"))
                                 .arg(hasFrame() ? QStringLiteral("1") : QStringLiteral("0"));
    }

    // Mark loading immediately so QML's spinner fallback (!isLoading) cannot
    // race ahead of CEF's OnLoadingStateChange and dismiss too early.
    if (!m_impl->isLoading) {
        m_impl->isLoading = true;
        emit loadingStateChanged(true);
    }
}

void BrowserView::closeCurrentPage()
{
    if (!m_impl->browser || m_impl->browserClosing) {
        m_impl->pendingUrl.clear();
        m_impl->currentUrl.clear();
        m_impl->currentTitle.clear();
        m_impl->canGoBack = false;
        m_impl->canGoForward = false;
        if (m_impl->audiblePlaybackActive) {
            setAudiblePlaybackActive(false);
        }
        clearVideoPlaybackState();
        return;
    }

    const QString currentUrl = m_impl->currentUrl;
    const QString currentTitle = m_impl->currentTitle;
    m_impl->pendingUrl.clear();
    m_impl->currentUrl.clear();
    m_impl->currentTitle.clear();
    m_impl->canGoBack = false;
    m_impl->canGoForward = false;
    if (m_impl->audiblePlaybackActive) {
        setAudiblePlaybackActive(false);
    }
    clearVideoPlaybackState();
    m_impl->browserClosing = true;
    if (CefRefPtr<CefFrame> frame = m_impl->browser->GetMainFrame()) {
        frame->ExecuteJavaScript(stopMediaPlaybackScript().toStdString(), frame->GetURL(), 0);
    }
    m_impl->browser->StopLoad();
    m_impl->browser->GetHost()->CloseBrowser(true);
}

void BrowserView::goBack()
{
    if (m_impl->browser && m_impl->browser->CanGoBack()) {
        logZoomBack("goBack(before)", this, m_impl->browser, m_impl->currentUrl);
        if (zoomBackDebugEnabled()) {
            m_impl->zoomBackPaintSamplesRemaining = 8;
        }
        m_impl->browser->GoBack();
        logZoomBack("goBack(after-call)", this, m_impl->browser);
    }
}

void BrowserView::goForward()
{
    if (m_impl->browser && m_impl->browser->CanGoForward()) {
        logZoomBack("goForward(before)", this, m_impl->browser, m_impl->currentUrl);
        m_impl->browser->GoForward();
        logZoomBack("goForward(after-call)", this, m_impl->browser);
    }
}

void BrowserView::reloadPage()
{
    if (m_impl->browser) {
        m_impl->browser->ReloadIgnoreCache();
    }
}

void BrowserView::setZoomFactor(double factor)
{
    const double previous = m_impl->zoomFactor;
    m_impl->zoomFactor = std::clamp(factor, 0.8, 3.0);
    if (m_impl->browser) {
        const double beforeLevel = m_impl->browser->GetHost()->GetZoomLevel();
        m_impl->browser->GetHost()->SetZoomLevel(zoomFactorToLevel(m_impl->zoomFactor));
        if (zoomBackDebugEnabled()) {
            logZoomBack("setZoomFactor", this, m_impl->browser,
                        QStringLiteral("fromDesired=%1 toDesired=%2 cefBefore=%3")
                            .arg(previous, 0, 'f', 3)
                            .arg(m_impl->zoomFactor, 0, 'f', 3)
                            .arg(beforeLevel, 0, 'f', 3));
        }
    }
}

void BrowserView::setMinimumFontSize(int size)
{
    m_impl->minimumFontSize = std::clamp(size, 12, 28);
    CefRefPtr<CefRequestContext> requestContext = m_impl->browser
        ? m_impl->browser->GetHost()->GetRequestContext()
        : CefRequestContext::GetGlobalContext();
    applyWebKitFontSizePreferences(requestContext, m_impl->minimumFontSize);
}

void BrowserView::syncPointerFromGlobal(const QPoint &globalPos, Qt::MouseButtons buttons)
{
    if (!m_impl->browser) {
        return;
    }

    m_impl->pressedMouseButtons = buttons;
    sendMouseMoveToBrowser(itemToCefPoint(mapPointFromGlobal(globalPos)), m_impl->pressedMouseButtons);
}

void BrowserView::sendMouseButtonFromGlobal(const QPoint &globalPos,
                                            Qt::MouseButton button,
                                            bool mouseUp,
                                            Qt::MouseButtons buttons)
{
    if (!m_impl->browser) {
        return;
    }

    m_impl->pressedMouseButtons = buttons;
    const QPoint local = itemToCefPoint(mapPointFromGlobal(globalPos));
    sendMouseMoveToBrowser(local, buttons);
    sendMouseClickToBrowser(local, button, mouseUp, buttons);
}

void BrowserView::beginTouchDragFromGlobal(const QPoint &globalPos)
{
    if (!m_impl->browser) {
        return;
    }

    requestNativeFocus();
    m_impl->selectionDragActive = true;
    const QPoint local = itemToCefPoint(mapPointFromGlobal(globalPos));
    m_impl->pressedMouseButtons = Qt::LeftButton;
    sendMouseMoveToBrowser(local, m_impl->pressedMouseButtons);
    sendMouseClickToBrowser(local, Qt::LeftButton, false, m_impl->pressedMouseButtons);
}

void BrowserView::updateTouchDragFromGlobal(const QPoint &globalPos)
{
    if (!m_impl->browser || !m_impl->selectionDragActive) {
        return;
    }

    m_impl->pressedMouseButtons = Qt::LeftButton;
    const QPoint local = itemToCefPoint(mapPointFromGlobal(globalPos));
    sendMouseMoveToBrowser(local, m_impl->pressedMouseButtons);
}

void BrowserView::endTouchDragFromGlobal(const QPoint &globalPos)
{
    if (!m_impl->browser || !m_impl->selectionDragActive) {
        return;
    }

    const QPoint local = itemToCefPoint(mapPointFromGlobal(globalPos));
    sendMouseMoveToBrowser(local, Qt::LeftButton);
    sendMouseClickToBrowser(local, Qt::LeftButton, true, Qt::NoButton);
    m_impl->pressedMouseButtons = Qt::NoButton;
    m_impl->selectionDragActive = false;
}

void BrowserView::requestNativeFocus()
{
    if (QQuickWindow *quickWindow = window()) {
        quickWindow->requestActivate();
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
        requestWindowInputFocus(static_cast<Window>(quickWindow->winId()));
#endif
    }

    forceActiveFocus(Qt::OtherFocusReason);

    if (m_impl->browser) {
        m_impl->browser->GetHost()->SetFocus(true);
    }
}

void BrowserView::setNativeCursorVisible(bool visible)
{
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    const Window topLevelWindow = window() ? static_cast<Window>(window()->winId()) : 0;
    setWindowCursorVisible(topLevelWindow, useCustomCursorRendering() ? false : visible);
#else
    Q_UNUSED(visible);
#endif
}

void BrowserView::scrollByPixels(const QPoint &delta, const QPoint &globalPos)
{
    if (!m_impl->browser || delta.isNull()) {
        return;
    }

    CefRefPtr<CefFrame> frame = m_impl->browser->GetMainFrame();
    if (!frame) {
        return;
    }

    const QSize cefSize = cefViewSize();
    const QPoint localPos = itemToCefPoint(mapPointFromGlobal(globalPos));
    const QPoint viewportPos(std::clamp(localPos.x(), 0, cefSize.width() - 1),
                             std::clamp(localPos.y(), 0, cefSize.height() - 1));
    frame->ExecuteJavaScript(directScrollScript(delta, viewportPos).toStdString(), frame->GetURL(), 0);
}

void BrowserView::sendWheelStep(const QPoint &angleDelta)
{
    if (!m_impl->browser) {
        return;
    }

    const QSize cefSize = cefViewSize();
    const QPoint localPos = itemToCefPoint(mapPointFromGlobal(QCursor::pos()));
    CefMouseEvent event;
    event.x = std::clamp(localPos.x(), 0, cefSize.width() - 1);
    event.y = std::clamp(localPos.y(), 0, cefSize.height() - 1);
    event.modifiers = qtMouseButtonsToCefModifiers(m_impl->pressedMouseButtons);

    if (angleDelta.y() != 0) {
        m_impl->browser->GetHost()->SendMouseWheelEvent(event, 0, angleDelta.y());
    }

    if (angleDelta.x() != 0) {
        CefMouseEvent horizontalEvent = event;
        horizontalEvent.modifiers |= EVENTFLAG_SHIFT_DOWN;
        m_impl->browser->GetHost()->SendMouseWheelEvent(horizontalEvent, 0, -angleDelta.x());
    }
}

void BrowserView::sendNavigationKey(int key)
{
    if (!m_impl->browser) {
        return;
    }

    const int cefKey = BrowserInputUtils::cefKeyForQtKey(key);
    if (cefKey == 0) {
        return;
    }

    CefKeyEvent pressEvent;
    pressEvent.type = KEYEVENT_RAWKEYDOWN;
    pressEvent.windows_key_code = cefKey;
    pressEvent.native_key_code = cefKey;
    m_impl->browser->GetHost()->SendKeyEvent(pressEvent);

    CefKeyEvent releaseEvent;
    releaseEvent.type = KEYEVENT_KEYUP;
    releaseEvent.windows_key_code = cefKey;
    releaseEvent.native_key_code = cefKey;
    m_impl->browser->GetHost()->SendKeyEvent(releaseEvent);
}

void BrowserView::commitOskText(const QString &text)
{
    if (text.isEmpty() || !m_impl->browser) {
        return;
    }

    CefRefPtr<CefBrowserHost> host = m_impl->browser->GetHost();
    if (!host) {
        return;
    }

    // Commit IME text at the caret in the focused editable element. This supports non-ASCII text
    // without synthesizing key codes.
    host->ImeCommitText(text.toStdString(), CefRange::InvalidRange(), 0);
}

void BrowserView::forwardInputMethodEvent(QInputMethodEvent *event)
{
    inputMethodEvent(event);
}

void BrowserView::paint(QPainter *painter)
{
    const QRectF bounds(0, 0, width(), height());
    painter->fillRect(bounds, Qt::black);

    // Called on the scene-graph thread concurrently with main-thread handleOsrPaint(). Hold the
    // lock while reading and drawing to avoid tearing from a partially copied frame.
    QMutexLocker locker(&m_impl->frameMutex);
    if (!m_impl->viewImage.isNull()) {
        // Fill the item because inactive tabs may hold a smaller frame from old geometry (for
        // example, space reserved for the operation bar or a pending resize). Drawing at 1:1 from
        // (0,0) would briefly show a shrunken image with black borders when returning to the tab.
        // drawImage(bounds) remains 1:1 when the frame and item sizes match.
        painter->drawImage(bounds, m_impl->viewImage);
    }

    if (m_impl->popupVisible && !m_impl->popupImage.isNull() && !m_impl->popupRect.isEmpty()) {
        painter->drawImage(m_impl->popupRect.topLeft(), m_impl->popupImage);
    }
}

void BrowserView::geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry)
{
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        resizeBrowserToView();
    }
}

void BrowserView::itemChange(ItemChange change, const ItemChangeData &value)
{
    QQuickPaintedItem::itemChange(change, value);

    // Design note (master parity, defensive): ItemVisibleHasChanged fires
    // whenever *effective* visibility changes, including ancestor hides
    // (e.g. the whole browser page being hidden when the user navigates to
    // the home screen). Reacting to it with WasHidden(true/false) would
    // introduce compositor stop/restart cycles that the QWidget master
    // branch never had, so it is deliberately ignored here. Browser
    // lifecycle is driven exclusively by the explicit setTabActive()
    // contract from BrowserContainer; Qt Quick still skips painting hidden
    // subtrees, so there is no rendering cost. (The actual pure-background
    // bug was a homeVisible desync in the LT/RT path — fixed in
    // MainWindow.qml switchPage — not a WasHidden stall; this remains as
    // hardening to keep parity with master.)
}

void BrowserView::applyVisibleState(bool visible)
{
    if (m_impl->browser) {
        m_impl->browser->GetHost()->WasHidden(!visible);
    }

    if (visible) {
        // Force Qt Quick to repaint the cached last frame immediately so the
        // user sees frozen content (not black) while waiting for fresh frames.
        // Re-apply zoom after WasHidden(false): CEF can reset zoom on the
        // compositor resume path; without this, close-tab reveal briefly
        // shows pageZoom≈1.0 then jumps back to the site factor (e.g. 1.78).
        if (m_impl->browser && !m_impl->browserClosing) {
            m_impl->browser->GetHost()->SetZoomLevel(zoomFactorToLevel(m_impl->zoomFactor));
        }
        update();
        setNativeCursorVisible(cursor().shape() != Qt::BlankCursor);
        scheduleCreateBrowserIfNeeded();
        armFrameWatchdog();
        QTimer::singleShot(0, this, [this]() {
            resizeBrowserToView();
            setNativeCursorVisible(cursor().shape() != Qt::BlankCursor);
        });
        QTimer::singleShot(100, this, [this]() {
            resizeBrowserToView();
            setNativeCursorVisible(cursor().shape() != Qt::BlankCursor);
        });
    } else {
        stopFrameWatchdog();
    }
}

void BrowserView::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        emit tabPressed();
        event->accept();
        return;
    }

    sendKeyEventToBrowser(event, false);
    event->accept();
}

void BrowserView::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Tab && event->modifiers() == Qt::NoModifier) {
        event->accept();
        return;
    }

    sendKeyEventToBrowser(event, true);
    event->accept();
}

void BrowserView::sendKeyEventToBrowser(QKeyEvent *event, bool keyUp)
{
    if (!m_impl->browser || !event) {
        return;
    }

    const int cefKey = BrowserInputUtils::cefKeyForQtKey(event->key());
    if (cefKey == 0 && event->text().isEmpty()) {
        return;
    }

    CefKeyEvent cefEvent;
    cefEvent.type = keyUp ? KEYEVENT_KEYUP : KEYEVENT_RAWKEYDOWN;
    cefEvent.windows_key_code = cefKey != 0 ? cefKey : event->text().front().unicode();
    cefEvent.native_key_code = cefEvent.windows_key_code;
    cefEvent.modifiers = BrowserInputUtils::cefModifiersForQtModifiers(event->modifiers());
    cefEvent.is_system_key = event->modifiers().testFlag(Qt::AltModifier);
    if (event->isAutoRepeat()) {
        cefEvent.modifiers |= EVENTFLAG_IS_REPEAT;
    }

    const bool hasShortcutModifier =
        event->modifiers().testFlag(Qt::ControlModifier)
        || event->modifiers().testFlag(Qt::AltModifier)
        || event->modifiers().testFlag(Qt::MetaModifier);
    const bool isAltGrText =
        event->modifiers().testFlag(Qt::ControlModifier)
        && event->modifiers().testFlag(Qt::AltModifier)
        && !event->modifiers().testFlag(Qt::MetaModifier)
        && std::all_of(event->text().cbegin(), event->text().cend(),
                       [](QChar character) { return character.isPrint(); });
    if (!keyUp && !event->text().isEmpty() && (!hasShortcutModifier || isAltGrText)) {
        m_impl->browser->GetHost()->SendKeyEvent(cefEvent);
        for (const QChar character : event->text()) {
            CefKeyEvent charEvent = cefEvent;
            charEvent.type = KEYEVENT_CHAR;
            charEvent.character = character.unicode();
            charEvent.unmodified_character = charEvent.character;
            m_impl->browser->GetHost()->SendKeyEvent(charEvent);
        }
        return;
    }

    m_impl->browser->GetHost()->SendKeyEvent(cefEvent);
}

void BrowserView::inputMethodEvent(QInputMethodEvent *event)
{
    if (!event || !m_impl->browser) {
        return;
    }

    CefRefPtr<CefBrowserHost> host = m_impl->browser->GetHost();
    if (!host) {
        return;
    }

    const QString commit = event->commitString();
    const QString preedit = event->preeditString();
    if (!commit.isEmpty()) {
        host->ImeCommitText(commit.toStdString(), CefRange::InvalidRange(), 0);
    }

    if (!preedit.isEmpty()) {
        int cursorPosition = preedit.size();
        for (const QInputMethodEvent::Attribute &attribute : event->attributes()) {
            if (attribute.type == QInputMethodEvent::Cursor) {
                cursorPosition = std::clamp(attribute.start, 0, int(preedit.size()));
                break;
            }
        }

        std::vector<CefCompositionUnderline> underlines;
        host->ImeSetComposition(preedit.toStdString(),
                                underlines,
                                CefRange::InvalidRange(),
                                CefRange(cursorPosition, cursorPosition));
    } else if (commit.isEmpty()) {
        host->ImeCancelComposition();
    }

    event->accept();
}

QVariant BrowserView::inputMethodQuery(Qt::InputMethodQuery query) const
{
    switch (query) {
    case Qt::ImEnabled:
        return true;
    case Qt::ImHints:
        return QVariant::fromValue(Qt::ImhNone);
    case Qt::ImCursorRectangle:
        return m_impl->imeCursorRect;
    default:
        return QQuickPaintedItem::inputMethodQuery(query);
    }
}

void BrowserView::focusInEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusInEvent(event);
    setNativeCursorVisible(cursor().shape() != Qt::BlankCursor);

    if (QQuickWindow *quickWindow = window()) {
        quickWindow->requestActivate();
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
        requestWindowInputFocus(static_cast<Window>(quickWindow->winId()));
#endif
    }

    if (m_impl->browser) {
        m_impl->browser->GetHost()->SetFocus(true);
    }
    setNativeCursorVisible(cursor().shape() != Qt::BlankCursor);
}

void BrowserView::focusOutEvent(QFocusEvent *event)
{
    QQuickPaintedItem::focusOutEvent(event);

    if (m_impl->browser) {
        m_impl->browser->GetHost()->ImeCancelComposition();
        m_impl->browser->GetHost()->SetFocus(false);
    }
    m_impl->imeCursorRect = {};
}

void BrowserView::hoverMoveEvent(QHoverEvent *event)
{
    // During selection dragging (LB+A), updateTouchDragFromGlobal is the authoritative pointer
    // source. Qt Quick resends HoverMove at the previous hover position on each frame while an item
    // is dirty. Forwarding those stale positions with the pressed left button would create a second
    // drag competing with joystick input, making the selection endpoint flicker. The QWidget
    // version does not synthesize these events; discard hover events while dragging.
    if (m_impl->selectionDragActive) {
        event->accept();
        return;
    }
    sendMouseMoveToBrowser(itemToCefPoint(event->position().toPoint()), m_impl->pressedMouseButtons);
    event->accept();
}

void BrowserView::mouseMoveEvent(QMouseEvent *event)
{
    sendMouseMoveToBrowser(itemToCefPoint(event->position().toPoint()), event->buttons());
    event->accept();
}

void BrowserView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        forceActiveFocus(Qt::MouseFocusReason);
        m_impl->pressedMouseButtons = event->buttons();
        sendMouseClickToBrowser(itemToCefPoint(event->position().toPoint()), event->button(), false, event->buttons());
        event->accept();
        return;
    }

    QQuickPaintedItem::mousePressEvent(event);
}

void BrowserView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton || event->button() == Qt::MiddleButton || event->button() == Qt::RightButton) {
        sendMouseClickToBrowser(itemToCefPoint(event->position().toPoint()), event->button(), true, event->buttons());
        m_impl->pressedMouseButtons = event->buttons();
        event->accept();
        return;
    }

    QQuickPaintedItem::mouseReleaseEvent(event);
}

void BrowserView::wheelEvent(QWheelEvent *event)
{
    sendWheelStep(event->angleDelta());
    event->accept();
}

void BrowserView::handleAddressSignal(const QString &url)
{
    m_impl->currentUrl = url;
}

void BrowserView::handleTitleSignal(const QString &title)
{
    m_impl->currentTitle = title;
    refreshNativeBrowserWindowHints();
}

void BrowserView::handleNavigationSignal(bool canGoBack, bool canGoForward)
{
    m_impl->canGoBack = canGoBack;
    m_impl->canGoForward = canGoForward;
}

void BrowserView::handleBrowserClosed()
{
    qInfo() << "BrowserView::handleBrowserClosed view=" << static_cast<const void *>(this);
    stopFrameWatchdog();
    m_impl->browser = nullptr;
    m_impl->client = nullptr;
    m_impl->browserClosing = false;
    m_impl->fullscreenMode = false;
    if (m_impl->audiblePlaybackActive) {
        setAudiblePlaybackActive(false);
    }
    clearVideoPlaybackState();
    m_impl->lastGpuDiagnosticsSignature.clear();
    m_impl->selectionDragActive = false;
    {
        QMutexLocker locker(&m_impl->frameMutex);
        m_impl->viewImage = QImage();
        m_impl->popupImage = QImage();
        m_impl->popupRect = QRect();
        m_impl->popupVisible = false;
    }
    update();

    emit browserClosed();

    if (!m_impl->pendingUrl.isEmpty()) {
        scheduleCreateBrowserIfNeeded();
    }
}

void BrowserView::setAudiblePlaybackActive(bool active)
{
    const bool wasMediaPlaybackActive = isMediaPlaybackActive();
    if (m_impl->audiblePlaybackActive == active) {
        return;
    }

    m_impl->audiblePlaybackActive = active;
    emit audiblePlaybackChanged(active);

    if (wasMediaPlaybackActive != isMediaPlaybackActive()) {
        emit mediaPlaybackChanged(isMediaPlaybackActive());
    }
}

void BrowserView::setVideoPlaybackState(const QString &frameId,
                                        bool videoActive,
                                        bool pictureInPictureActive,
                                        bool fullscreenVideoActive)
{
    if (frameId.isEmpty()) {
        return;
    }

    quint8 nextState = 0;
    if (videoActive) {
        nextState |= 0x1;
    }
    if (pictureInPictureActive) {
        nextState |= 0x2;
    }
    if (fullscreenVideoActive) {
        nextState |= 0x4;
    }

    if (m_impl->lastVideoPlaybackStateByFrame.value(frameId, 0) == nextState) {
        return;
    }

    if (nextState == 0) {
        m_impl->lastVideoPlaybackStateByFrame.remove(frameId);
    } else {
        m_impl->lastVideoPlaybackStateByFrame.insert(frameId, nextState);
    }

    const bool wasMediaPlaybackActive = isMediaPlaybackActive();
    if (videoActive) {
        m_impl->activeVideoFrameIds.insert(frameId);
    } else {
        m_impl->activeVideoFrameIds.remove(frameId);
    }

    if (pictureInPictureActive) {
        m_impl->pictureInPictureFrameIds.insert(frameId);
    } else {
        m_impl->pictureInPictureFrameIds.remove(frameId);
    }

    if (fullscreenVideoActive) {
        m_impl->fullscreenVideoFrameIds.insert(frameId);
    } else {
        m_impl->fullscreenVideoFrameIds.remove(frameId);
    }

    if (wasMediaPlaybackActive != isMediaPlaybackActive()) {
        emit mediaPlaybackChanged(isMediaPlaybackActive());
    }
}

void BrowserView::clearVideoPlaybackState()
{
    const bool wasMediaPlaybackActive = isMediaPlaybackActive();
    if (m_impl->activeVideoFrameIds.isEmpty()) {
        if (m_impl->pictureInPictureFrameIds.isEmpty() && m_impl->fullscreenVideoFrameIds.isEmpty()) {
            m_impl->lastVideoPlaybackStateByFrame.clear();
            return;
        }
    }

    m_impl->activeVideoFrameIds.clear();
    m_impl->pictureInPictureFrameIds.clear();
    m_impl->fullscreenVideoFrameIds.clear();
    m_impl->lastVideoPlaybackStateByFrame.clear();
    if (wasMediaPlaybackActive != isMediaPlaybackActive()) {
        emit mediaPlaybackChanged(isMediaPlaybackActive());
    }
}

void BrowserView::refreshNativeBrowserWindowHints()
{
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    if (!m_impl->browser || m_impl->browser->GetHost()->IsWindowRenderingDisabled()) {
        return;
    }

    const Window topLevelWindow = window() ? static_cast<Window>(window()->winId()) : 0;
    const Window browserWindow = m_impl->browser ? static_cast<Window>(m_impl->browser->GetHost()->GetWindowHandle()) : 0;
    configureEmbeddedBrowserWindow(browserWindow, topLevelWindow);
#endif
}

CefRect BrowserView::viewRectForOsr() const
{
    const QSize size = cefViewSize();
    return CefRect(0, 0, size.width(), size.height());
}

bool BrowserView::fillScreenInfoForOsr(CefScreenInfo &screenInfo) const
{
    const QScreen *screen = window() ? window()->screen() : QGuiApplication::primaryScreen();
    if (!screen) {
        return false;
    }

    const qreal deviceScaleFactor = viewDevicePixelRatio();
    const QRect geometry = screen->geometry();
    const QRect availableGeometry = screen->availableGeometry();

    screenInfo.device_scale_factor = float(deviceScaleFactor);
    screenInfo.depth = screen->depth();
    screenInfo.depth_per_component = 8;
    screenInfo.is_monochrome = false;
    screenInfo.rect = CefRect(geometry.x(), geometry.y(), geometry.width(), geometry.height());
    screenInfo.available_rect = CefRect(availableGeometry.x(), availableGeometry.y(), availableGeometry.width(), availableGeometry.height());
    return true;
}

bool BrowserView::mapPointToScreenForOsr(int viewX, int viewY, int &screenX, int &screenY) const
{
    // Item-local coordinates are already CEF/screen logical pixels; mapToGlobal includes both
    // Item.scale and WindowBase scale.
    const QPointF global = mapToGlobal(QPointF(viewX, viewY));
    const qreal deviceScaleFactor = viewDevicePixelRatio();
    screenX = qRound(global.x() * deviceScaleFactor);
    screenY = qRound(global.y() * deviceScaleFactor);
    return true;
}

void BrowserView::handlePopupVisibility(bool visible)
{
    QRect previousRect;
    {
        QMutexLocker locker(&m_impl->frameMutex);
        previousRect = m_impl->popupRect;
        m_impl->popupVisible = visible;
        if (!visible) {
            m_impl->popupImage = QImage();
            m_impl->popupRect = QRect();
        }
    }

    if (previousRect.isEmpty()) {
        update();
    } else {
        update(previousRect.adjusted(-1, -1, 1, 1));
    }
}

void BrowserView::handlePopupRectChanged(const CefRect &rect)
{
    QRect updateRect;
    {
        QMutexLocker locker(&m_impl->frameMutex);
        const QRect previousRect = m_impl->popupRect;
        m_impl->popupRect = QRect(rect.x, rect.y, rect.width, rect.height);
        updateRect = previousRect.united(m_impl->popupRect).adjusted(-1, -1, 1, 1);
    }
    update(updateRect);
}

void BrowserView::handleImeCompositionRangeChanged(
    const CefRenderHandler::RectList &characterBounds)
{
    if (characterBounds.empty()) {
        m_impl->imeCursorRect = {};
    } else {
        const CefRect &rect = characterBounds.front();
        m_impl->imeCursorRect = QRectF(rect.x, rect.y, rect.width, rect.height);
    }

    if (QInputMethod *inputMethod = QGuiApplication::inputMethod()) {
        inputMethod->update(Qt::ImCursorRectangle);
    }
}

void BrowserView::handleOsrPaint(CefRenderHandler::PaintElementType type,
                                 const CefRenderHandler::RectList &dirtyRects,
                                 const QImage &frame)
{
    if (frame.isNull() || frame.width() <= 0 || frame.height() <= 0) {
        return;
    }

    const int width = frame.width();
    const int height = frame.height();
    const qreal deviceScaleFactor = viewDevicePixelRatio();
    m_impl->deviceScaleFactor = deviceScaleFactor;

    if (type == PET_POPUP) {
        QImage copied = frame.copy();
        copied.setDevicePixelRatio(deviceScaleFactor);
        QRect popupUpdateRect;
        {
            QMutexLocker locker(&m_impl->frameMutex);
            m_impl->popupImage = std::move(copied);
            m_impl->popupVisible = true;
            popupUpdateRect = m_impl->popupRect.adjusted(-1, -1, 1, 1);
        }
        update(popupUpdateRect);
        return;
    }

    // Watchdog bookkeeping: a healthy frame arrived.
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_impl->watchdogRecoveryAttempts > 0) {
        qInfo().noquote() << QStringLiteral("[osr] frames recovered after %1 reload attempt(s)")
                                 .arg(m_impl->watchdogRecoveryAttempts);
    }
    m_impl->watchdogRecoveryAttempts = 0;
    m_impl->watchdogLastFrameTime = now;

    // Hold the lock while writing buffers because paint() may read viewImage on the scene-graph
    // thread. Release it before update() and signal emission to avoid scene-graph work under lock.
    bool emitFrameAvailable = false;
    QRect dirtyUpdateRect;
    {
        QMutexLocker locker(&m_impl->frameMutex);
        const QSize nextSize(width, height);
        emitFrameAvailable = m_impl->viewImage.isNull() || m_impl->notifyNextFrame;
        m_impl->notifyNextFrame = false;
        const bool resetViewImage = m_impl->viewImage.size() != nextSize
            || m_impl->viewImage.format() != QImage::Format_ARGB32;

        if (resetViewImage) {
            m_impl->viewImage = QImage(nextSize, QImage::Format_ARGB32);
        }
        m_impl->viewImage.setDevicePixelRatio(deviceScaleFactor);

        if (resetViewImage || dirtyRects.empty()) {
            for (int y = 0; y < height; ++y) {
                std::memcpy(m_impl->viewImage.scanLine(y), frame.constScanLine(y), size_t(width) * 4u);
            }
        } else {
            QRegion dirtyRegion;
            for (const CefRect &dirtyRect : dirtyRects) {
                const QRect rect = QRect(dirtyRect.x, dirtyRect.y, dirtyRect.width, dirtyRect.height)
                                       .intersected(QRect(0, 0, width, height));
                if (rect.isEmpty()) {
                    continue;
                }

                for (int y = rect.top(); y <= rect.bottom(); ++y) {
                    std::memcpy(m_impl->viewImage.scanLine(y) + rect.left() * 4,
                                frame.constScanLine(y) + rect.left() * 4,
                                size_t(rect.width()) * 4u);
                }
                dirtyRegion += rect;
            }
            dirtyUpdateRect = dirtyRegion.boundingRect();
        }
    }

    if (emitFrameAvailable) {
        if (qEnvironmentVariableIsSet("AEROCORE_LOAD_TIMING")) {
            qInfo().noquote() << QStringLiteral("[load-timing] OSR firstFrameAvailable signal url=%1 size=%2x%3")
                                     .arg(m_impl->currentUrl)
                                     .arg(width)
                                     .arg(height);
        }
        emit firstFrameAvailable();
    }
    if (m_impl->zoomBackPaintSamplesRemaining > 0) {
        --m_impl->zoomBackPaintSamplesRemaining;
        const QSize itemSize = size().toSize();
        const qreal logicalW = width / std::max<qreal>(deviceScaleFactor, 0.01);
        const qreal logicalH = height / std::max<qreal>(deviceScaleFactor, 0.01);
        logZoomBack("OSR-paint", this, m_impl->browser,
                    QStringLiteral("framePx=%1x%2 frameLogical≈%3x%4 item=%5x%6 dpr=%7 left=%8")
                        .arg(width)
                        .arg(height)
                        .arg(qRound(logicalW))
                        .arg(qRound(logicalH))
                        .arg(itemSize.width())
                        .arg(itemSize.height())
                        .arg(deviceScaleFactor, 0, 'f', 3)
                        .arg(m_impl->zoomBackPaintSamplesRemaining));
    }
    if (dirtyUpdateRect.isEmpty()) {
        update();
    } else {
        update(dirtyUpdateRect);
    }
}

void BrowserView::bindQuickWindow(QQuickWindow *quickWindow)
{
    if (m_impl->windowSceneGraphConnection) {
        disconnect(m_impl->windowSceneGraphConnection);
        m_impl->windowSceneGraphConnection = {};
    }

    m_impl->winIdRetryCount = 0;
    if (!quickWindow) {
        return;
    }

    m_impl->windowSceneGraphConnection = connect(
        quickWindow, &QQuickWindow::sceneGraphInitialized,
        this, [this]() {
            m_impl->winIdRetryCount = 0;
            scheduleCreateBrowserIfNeeded();
            resizeBrowserToView();
        });

    scheduleCreateBrowserIfNeeded();
    resizeBrowserToView();
}

void BrowserView::scheduleCreateBrowserIfNeeded()
{
    if (!isViewActive() || m_impl->browser || m_impl->browserClosing) {
        return;
    }
    if (m_impl->pendingUrl.trimmed().isEmpty()) {
        // Tab may become active before loadUrl; wait for a real target so we
        // do not CreateBrowserSync with a stale/default URL.
        return;
    }

    createBrowserIfNeeded();
}

void BrowserView::createBrowserIfNeeded()
{
    if (m_impl->browser || m_impl->browserClosing || !isViewActive()) {
        return;
    }
    if (m_impl->pendingUrl.trimmed().isEmpty()) {
        return;
    }

    QQuickWindow *quickWindow = window();
    if (!quickWindow) {
        return;
    }

    constexpr int kMaxWinIdRetries = 60;
    const WId nativeId = quickWindow->winId();
    if (nativeId == 0) {
        if (m_impl->winIdRetryCount >= kMaxWinIdRetries) {
            qWarning().noquote() << QStringLiteral("BrowserView: giving up CreateBrowserSync; QQuickWindow winId still 0");
            return;
        }

        ++m_impl->winIdRetryCount;
        QTimer::singleShot(16, this, [this]() {
            scheduleCreateBrowserIfNeeded();
        });
        return;
    }

    m_impl->winIdRetryCount = 0;
    m_impl->client = new BrowserViewClient(this);

    const QSize viewSize = cefViewSize();
    const qint64 createStartMs = QDateTime::currentMSecsSinceEpoch();
    const bool logTiming = qEnvironmentVariableIsSet("AEROCORE_LOAD_TIMING");
    if (logTiming) {
        qInfo().noquote() << QStringLiteral("[load-timing] CreateBrowserSync begin url=%1 view=%2x%3 dpr=%4 tabActive=%5")
                                 .arg(m_impl->pendingUrl)
                                 .arg(viewSize.width())
                                 .arg(viewSize.height())
                                 .arg(viewDevicePixelRatio())
                                 .arg(m_impl->tabActive ? QStringLiteral("1") : QStringLiteral("0"));
    }

    CefWindowInfo windowInfo;
    const CefWindowHandle topLevelHandle =
#if defined(_WIN32)
        reinterpret_cast<CefWindowHandle>(nativeId);
#else
        static_cast<CefWindowHandle>(nativeId);
#endif
    windowInfo.SetAsWindowless(topLevelHandle);
    windowInfo.runtime_style = CEF_RUNTIME_STYLE_ALLOY;

    CefBrowserSettings browserSettings;
    browserSettings.javascript = STATE_ENABLED;
    browserSettings.local_storage = STATE_ENABLED;
    // Leave font sizes at 0 so PrefService (SetPreference) controls them. Non-zero
    // CefBrowserSettings values permanently override webkit.webprefs.* at runtime.
    browserSettings.windowless_frame_rate = 60;

    applyWebKitFontSizePreferences(CefRequestContext::GetGlobalContext(), m_impl->minimumFontSize);

    m_impl->browser = CefBrowserHost::CreateBrowserSync(
        windowInfo,
        m_impl->client,
        m_impl->pendingUrl.toStdString(),
        browserSettings,
        nullptr,
        CefRequestContext::GetGlobalContext());

    if (m_impl->browser) {
        qInfo() << "CreateBrowserSync done view=" << static_cast<const void *>(this)
                << "client=" << static_cast<const void *>(m_impl->client.get())
                << "browserId=" << m_impl->browser->GetIdentifier();
    }

    if (logTiming) {
        qInfo().noquote() << QStringLiteral("[load-timing] CreateBrowserSync done +%1ms browser=%2")
                                 .arg(QDateTime::currentMSecsSinceEpoch() - createStartMs)
                                 .arg(m_impl->browser ? m_impl->browser->GetIdentifier() : -1);
    }

    if (m_impl->browser) {
        m_impl->lastSentCefMouseValid = false;
        // OSR views start hidden (is_showing_ = false). Relying on the constructor's implicit Show
        // is timing-sensitive and can delay the first frame; Chromium also throttles hidden views.
        // Explicitly call WasHidden(false) so loading and frame delivery start promptly. This is a
        // no-op if the view is already visible and matches cefclient's BrowserWindowOsrGtk::Show().
        m_impl->browser->GetHost()->WasHidden(false);
        requestNativeFocus();
        resizeBrowserToView();
        setZoomFactor(m_impl->zoomFactor);
        setMinimumFontSize(m_impl->minimumFontSize);
        if (m_impl->audioMuted) {
            m_impl->browser->GetHost()->SetAudioMuted(true);
        }
        setNativeCursorVisible(cursor().shape() != Qt::BlankCursor);
        armFrameWatchdog();
        QTimer::singleShot(0, this, [this]() {
            resizeBrowserToView();
            setNativeCursorVisible(cursor().shape() != Qt::BlankCursor);
        });
        QTimer::singleShot(100, this, [this]() {
            resizeBrowserToView();
            setNativeCursorVisible(cursor().shape() != Qt::BlankCursor);
        });
    }
}

void BrowserView::resizeBrowserToView()
{
    if (!m_impl->browser) {
        return;
    }

    m_impl->deviceScaleFactor = viewDevicePixelRatio();
    m_impl->browser->GetHost()->NotifyScreenInfoChanged();
    m_impl->browser->GetHost()->WasResized();
    m_impl->browser->GetHost()->Invalidate(PET_VIEW);
}

void BrowserView::armFrameWatchdog()
{
    if (!m_impl->browser || !isViewActive()) {
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    // Treat the WasResized/Invalidate calls that accompany visibility changes
    // and browser creation as the initial poke.
    m_impl->watchdogLastPokeTime = now;
    m_impl->watchdogLastHeartbeat = now;
    if (!m_impl->frameWatchdogTimer.isActive()) {
        qInfo().noquote() << QStringLiteral("[osr] frame watchdog armed (browser=%1, viewImage=%2x%3)")
                                 .arg(m_impl->browser->GetIdentifier())
                                 .arg(m_impl->viewImage.width())
                                 .arg(m_impl->viewImage.height());
        m_impl->frameWatchdogTimer.start();
    }
}

void BrowserView::stopFrameWatchdog()
{
    if (m_impl->frameWatchdogTimer.isActive()) {
        m_impl->frameWatchdogTimer.stop();
    }
}

void BrowserView::onFrameWatchdogTimeout()
{
    if (!m_impl->browser || m_impl->browserClosing || !isViewActive()) {
        stopFrameWatchdog();
        return;
    }

    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    // Suspend/resume detection: wall-clock gap between heartbeats means the
    // system was suspended. Re-poke so the watchdog window restarts cleanly.
    if (m_impl->watchdogLastHeartbeat > 0 && now - m_impl->watchdogLastHeartbeat > 10000) {
        qInfo().noquote() << QStringLiteral("[osr] suspend/resume detected (gap=%1ms), re-poking renderer")
                                 .arg(now - m_impl->watchdogLastHeartbeat);
        m_impl->watchdogLastHeartbeat = now;
        m_impl->watchdogLastPokeTime = now;
        m_impl->watchdogStallChecks = 0;
        m_impl->browser->GetHost()->NotifyScreenInfoChanged();
        m_impl->browser->GetHost()->WasResized();
        m_impl->browser->GetHost()->Invalidate(PET_VIEW);
        return;
    }
    m_impl->watchdogLastHeartbeat = now;

    // Frames flowing normally — nothing to do.
    if (m_impl->watchdogLastFrameTime >= m_impl->watchdogLastPokeTime) {
        m_impl->watchdogStallChecks = 0;
        return;
    }

    // No frame arrived since the last poke.
    ++m_impl->watchdogStallChecks;

    if (m_impl->watchdogStallChecks < 3) {
        // Gentle recovery: poke the renderer compositor.
        m_impl->watchdogLastPokeTime = now;
        m_impl->browser->GetHost()->NotifyScreenInfoChanged();
        m_impl->browser->GetHost()->WasResized();
        m_impl->browser->GetHost()->Invalidate(PET_VIEW);
        return;
    }

    // ~9 seconds without any frame response: renderer is unresponsive.
    // Escalate to full re-navigation (the only reliable recovery).
    constexpr int kMaxRecoveryAttempts = 3;
    if (m_impl->watchdogRecoveryAttempts >= kMaxRecoveryAttempts) {
        qWarning().noquote() << QStringLiteral("[osr] renderer unresponsive after %1 reload attempts, giving up")
                                    .arg(m_impl->watchdogRecoveryAttempts);
        stopFrameWatchdog();
        return;
    }

    const QString url = !m_impl->currentUrl.trimmed().isEmpty()
        ? m_impl->currentUrl
        : m_impl->pendingUrl;
    if (url.trimmed().isEmpty()) {
        stopFrameWatchdog();
        return;
    }

    ++m_impl->watchdogRecoveryAttempts;
    m_impl->watchdogStallChecks = 0;
    m_impl->watchdogLastPokeTime = now;
    qWarning().noquote() << QStringLiteral("[osr] renderer stalled (no OSR frames, viewImage=%1), reloading url=%2 (attempt %3/%4)")
                                .arg(m_impl->viewImage.isNull() ? QStringLiteral("empty") : QStringLiteral("cached"))
                                .arg(url)
                                .arg(m_impl->watchdogRecoveryAttempts)
                                .arg(kMaxRecoveryAttempts);

    // The navigation itself provokes frames from a healthy renderer.
    m_impl->browser->GetMainFrame()->LoadURL(url.trimmed().toStdString());
}

void BrowserView::sendMouseMoveToBrowser(const QPoint &position, Qt::MouseButtons buttons)
{
    if (!m_impl->browser) {
        return;
    }

    const QSize cefSize = cefViewSize();
    CefMouseEvent event;
    event.x = std::clamp(position.x(), 0, cefSize.width() - 1);
    event.y = std::clamp(position.y(), 0, cefSize.height() - 1);
    event.modifiers = qtMouseButtonsToCefModifiers(buttons);

    // Suppress duplicate events caused by Qt Quick's frame-synchronous synthetic
    // hover delivery (flushFrameSynchronousEvents re-sends HoverMove every frame
    // while any item is dirty). Without this, pages see a 60 fps mousemove
    // stream during video playback and player controls never auto-hide.
    const QPoint sentPos(event.x, event.y);
    if (m_impl->lastSentCefMouseValid
        && m_impl->lastSentCefMousePos == sentPos
        && m_impl->lastSentCefMouseButtons == buttons) {
        return;
    }
    m_impl->lastSentCefMouseValid = true;
    m_impl->lastSentCefMousePos = sentPos;
    m_impl->lastSentCefMouseButtons = buttons;

    m_impl->browser->GetHost()->SendMouseMoveEvent(event, false);
}

void BrowserView::sendMouseClickToBrowser(const QPoint &position, Qt::MouseButton button, bool mouseUp, Qt::MouseButtons buttons)
{
    if (!m_impl->browser) {
        qWarning() << "sendMouseClickToBrowser skipped: no CEF browser";
        return;
    }

    qInfo() << "sendMouseClickToBrowser pos=" << position << "button=" << button << "mouseUp=" << mouseUp;

    const QSize cefSize = cefViewSize();
    CefMouseEvent event;
    event.x = std::clamp(position.x(), 0, cefSize.width() - 1);
    event.y = std::clamp(position.y(), 0, cefSize.height() - 1);
    event.modifiers = qtMouseButtonsToCefModifiers(buttons);
    m_impl->browser->GetHost()->SendMouseClickEvent(event, qtMouseButtonToCefButton(button), mouseUp, 1);
}
