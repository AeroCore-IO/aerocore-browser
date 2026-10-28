#ifndef BROWSERCONTAINER_H
#define BROWSERCONTAINER_H

#include <QHash>
#include <QQuickItem>
#include <QTimer>
#include <QPointer>
#include "browsercontroller.h"
#include "browserview.h"

class InputManager;
class MainController;
class SiteConfigManager;

class BrowserContainerTestAccess;

class BrowserContainer : public QQuickItem
{
    QML_ELEMENT
    Q_OBJECT

    friend class BrowserContainerTestAccess;

    Q_PROPERTY(qreal scaleFactor READ scaleFactor WRITE setScaleFactor)
    Q_PROPERTY(bool atNavigationStart READ atNavigationStart NOTIFY atNavigationStartChanged)
    Q_PROPERTY(bool isLoading READ isLoading NOTIFY isLoadingChanged)

public:
    explicit BrowserContainer(QQuickItem *parent = nullptr);
    ~BrowserContainer();

    qreal scaleFactor() { return m_scaleFactor; }
    void setScaleFactor(qreal factor);

    Q_INVOKABLE void setBrowserController(BrowserController* controller);
    BrowserController *browserController() const { return m_browserController; }
    void setInputManager(InputManager *inputManager);
    void setMainController(MainController *controller);
    void setSiteConfigManager(SiteConfigManager *siteConfig);
    void applyReadability(const QString &url);

    void loadBrowserTab(const QString &tabId, const QString &url, bool forceReload);
    void enqueueBrowserViewRemovals(const QStringList &tabIds);
    void closeActivePage();

    bool atNavigationStart() const;
    Q_INVOKABLE bool isAtNavigationStart() const { return atNavigationStart(); }

    bool isLoading() const;

    // Media playback state of the active view (master parity — used by the
    // playback power-inhibit logic in MainController).
    bool hasActiveView() const;
    bool isAudiblePlaybackActive() const;
    bool isVideoPlaybackActive() const;
    bool isPictureInPicturePlaybackActive() const;
    bool isFullscreenVideoPlaybackActive() const;
    bool isMediaPlaybackActive() const;

    // True once the active tab has rendered at least one frame; the QML
    // loading spinner uses this to reveal the page early (progressive
    // rendering) instead of waiting for the full load to complete.
    Q_INVOKABLE bool hasFrame() const;

    // Homepage→browser open latency tracing. Set AEROCORE_LOAD_TIMING=1 to enable.
    // Grep logs for "[load-timing]".
    Q_INVOKABLE bool isLoadTimingEnabled() const;
    Q_INVOKABLE quint64 beginLoadTiming(const QString &source, const QString &url);
    Q_INVOKABLE void markLoadTiming(const QString &stage);
    Q_INVOKABLE qint64 loadTimingElapsedMs() const;

    Q_INVOKABLE void goBack();
    Q_INVOKABLE void goForward();
    Q_INVOKABLE void reloadPage();
    Q_INVOKABLE void adjustPageZoom(double delta);
    Q_INVOKABLE void adjustMinimumFontSize(int delta);
    Q_INVOKABLE void applyMinimumFontSize(int size);

signals:
    void loadFailed(const QString &url, const QString &errorText);
    void renderProcessCrashed(const QString &errorText);
    void activeFrameAvailable();
    void atNavigationStartChanged();
    void isLoadingChanged();
    void mediaPlaybackChanged(bool active);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void itemChange(ItemChange change, const ItemChangeData &value) override;

private slots:
    void onThumbnailCaptureTimeout();
    void processPendingBrowserViewRemoval();

private:
    void syncActiveViewGeometry();
    void syncActiveBrowserViewToInputManager();
    BrowserView *ensureBrowserView(const QString &tabId);
    void showBrowserTab(const QString &tabId);
    void removeBrowserView(const QString &tabId);
    void syncBrowserControllerFromView(BrowserView *view);
    void connectBrowserViewSignals(BrowserView *view);
    void captureHistoryThumbnailForUrl(const QString &url, int attempt = 0);
    void connectInputManager();
    void disconnectInputManager();
    void notifyAtNavigationStartChanged();
    void notifyIsLoadingChanged();
    void logLoadTiming(const QString &stage) const;

private:
    BrowserController* m_browserController = nullptr;
    InputManager *m_inputManager = nullptr;
    MainController *m_mainController = nullptr;
    SiteConfigManager *m_siteConfig = nullptr;
    QHash<QString, BrowserView *> m_browserViews;
    QString m_activeTabId;
    BrowserView* m_browserView = nullptr;
    QStringList m_pendingBrowserViewRemovals;
    bool m_browserViewRemovalScheduled = false;
    qreal m_scaleFactor = 1.0;
    QTimer m_thumbnailCaptureTimer;
    QString m_pendingThumbnailUrl;
    bool m_atNavigationStart = true;
    bool m_isLoading = false;
    quint64 m_loadTimingId = 0;
    qint64 m_loadTimingStartMs = 0;
    QString m_loadTimingUrl;
    QString m_loadTimingSource;

    QMetaObject::Connection m_pointerButtonConnection;
    QMetaObject::Connection m_pointerTouchConnection;
    QMetaObject::Connection m_wheelConnection;
    QMetaObject::Connection m_navigationConnection;
};

#endif
