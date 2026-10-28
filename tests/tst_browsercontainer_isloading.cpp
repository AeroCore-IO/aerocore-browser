#include "browsercontainerquickitem.h"
#include "browserview.h"
#include "browserview_test_access.h"

#include <QCoreApplication>
#include <QEvent>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

#include "test_util_gui.h"

class BrowserContainerTestAccess
{
public:
    static BrowserView *browserView(BrowserContainer *container, const QString &tabId)
    {
        return container ? container->m_browserViews.value(tabId, nullptr) : nullptr;
    }
};

class TestBrowserContainerIsLoading : public QObject
{
    Q_OBJECT

private slots:
    void isLoadingReturnsFalseWithoutActiveView();
    void isLoadingReflectsActiveBrowserView();
    void isLoadingChangedNotEmittedForInactiveTab();
    void switchingActiveTabUpdatesIsLoading();
    void duplicateLoadingStateDoesNotEmitSignal();
    void warmResumeWithCachedFrameEmitsActiveFrameAvailable();
    void urlChangeKeepsCachedFrameAndNotifiesOnNextPaint();
    void scaleFactorKeepsCefPixelsAlignedWithWindow();
    void destroyingViewAttachedToWindowDoesNotCrash();
};

void TestBrowserContainerIsLoading::isLoadingReturnsFalseWithoutActiveView()
{
    BrowserContainer container;
    QVERIFY(!container.isLoading());
}

void TestBrowserContainerIsLoading::isLoadingReflectsActiveBrowserView()
{
    BrowserContainer container;
    QSignalSpy spy(&container, &BrowserContainer::isLoadingChanged);

    container.loadBrowserTab(QStringLiteral("tab-1"), QString(), false);
    QVERIFY(BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-1")));
    QCOMPARE(container.isLoading(), false);
    QCOMPARE(spy.count(), 0);

    BrowserView *view = BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-1"));
    BrowserViewTestAccess::setLoading(view, true);
    QCOMPARE(container.isLoading(), true);
    QCOMPARE(spy.count(), 1);

    BrowserViewTestAccess::setLoading(view, false);
    QCOMPARE(container.isLoading(), false);
    QCOMPARE(spy.count(), 2);
}

void TestBrowserContainerIsLoading::isLoadingChangedNotEmittedForInactiveTab()
{
    BrowserContainer container;
    QSignalSpy spy(&container, &BrowserContainer::isLoadingChanged);

    container.loadBrowserTab(QStringLiteral("tab-1"), QString(), false);
    container.loadBrowserTab(QStringLiteral("tab-2"), QString(), false);
    QCOMPARE(spy.count(), 0);

    BrowserView *inactiveView = BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-1"));
    BrowserViewTestAccess::setLoading(inactiveView, true);
    QCOMPARE(spy.count(), 0);
    QVERIFY(!container.isLoading());
}

void TestBrowserContainerIsLoading::switchingActiveTabUpdatesIsLoading()
{
    BrowserContainer container;
    QSignalSpy spy(&container, &BrowserContainer::isLoadingChanged);

    container.loadBrowserTab(QStringLiteral("tab-1"), QString(), false);
    BrowserView *tab1 = BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-1"));
    BrowserViewTestAccess::setLoading(tab1, true);
    QCOMPARE(container.isLoading(), true);
    QCOMPARE(spy.count(), 1);

    container.loadBrowserTab(QStringLiteral("tab-2"), QString(), false);
    QCOMPARE(container.isLoading(), false);
    QCOMPARE(spy.count(), 2);

    container.loadBrowserTab(QStringLiteral("tab-1"), QString(), false);
    QCOMPARE(container.isLoading(), true);
    QCOMPARE(spy.count(), 3);
}

void TestBrowserContainerIsLoading::duplicateLoadingStateDoesNotEmitSignal()
{
    BrowserContainer container;
    QSignalSpy spy(&container, &BrowserContainer::isLoadingChanged);

    container.loadBrowserTab(QStringLiteral("tab-1"), QString(), false);
    BrowserView *view = BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-1"));

    BrowserViewTestAccess::setLoading(view, true);
    QCOMPARE(spy.count(), 1);

    emit view->loadingStateChanged(true);
    QCOMPARE(spy.count(), 1);
}

void TestBrowserContainerIsLoading::warmResumeWithCachedFrameEmitsActiveFrameAvailable()
{
    // Homepage openSite warm-resume keeps the cached OSR frame and never
    // re-emits BrowserView::firstFrameAvailable. loadBrowserTab must tell
    // QML immediately so the loading spinner can dismiss without a poll delay.
    BrowserContainer container;
    QSignalSpy spy(&container, &BrowserContainer::activeFrameAvailable);

    container.loadBrowserTab(QStringLiteral("tab-1"), QStringLiteral("https://example.com"), false);
    QCOMPARE(spy.count(), 0);

    BrowserView *view = BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-1"));
    QVERIFY(view);
    BrowserViewTestAccess::setHasFrame(view, true);
    QVERIFY(container.hasFrame());

    container.loadBrowserTab(QStringLiteral("tab-1"), QStringLiteral("https://example.com"), false);
    QCOMPARE(spy.count(), 1);
}

void TestBrowserContainerIsLoading::urlChangeKeepsCachedFrameAndNotifiesOnNextPaint()
{
    // Navigating away must not blank the cached OSR frame (keeps UI snappy under
    // the spinner). It also must not warm-emit immediately; the next paint armed
    // by loadUrl should forward firstFrameAvailable → activeFrameAvailable.
    BrowserContainer container;
    QSignalSpy frameSpy(&container, &BrowserContainer::activeFrameAvailable);

    container.loadBrowserTab(QStringLiteral("tab-1"), QStringLiteral("https://example.com"), false);
    BrowserView *view = BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-1"));
    BrowserViewTestAccess::setLoading(view, false);
    BrowserViewTestAccess::setHasFrame(view, true);
    QVERIFY(container.hasFrame());
    frameSpy.clear();

    container.loadBrowserTab(QStringLiteral("tab-1"), QStringLiteral("https://other.example"), false);
    QCOMPARE(frameSpy.count(), 0);
    QVERIFY(container.hasFrame());
    QCOMPARE(container.isLoading(), true);
    QVERIFY(BrowserViewTestAccess::notifyNextFrameArmed(view));

    BrowserViewTestAccess::deliverPendingFrameNotification(view);
    QCOMPARE(frameSpy.count(), 1);
    QVERIFY(container.hasFrame());
    QVERIFY(!BrowserViewTestAccess::notifyNextFrameArmed(view));
}

void TestBrowserContainerIsLoading::scaleFactorKeepsCefPixelsAlignedWithWindow()
{
    BrowserContainer container;
    container.setSize(QSizeF(1920, 1080));
    container.setScaleFactor(0.5);
    container.loadBrowserTab(QStringLiteral("tab-1"), QString(), false);

    BrowserView *view = BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-1"));
    QVERIFY(view);
    QCOMPARE(view->size(), QSizeF(960, 540));
    QCOMPARE(view->scale(), 2.0);
    QCOMPARE(view->uiScaleFactor(), 0.5);
}

void TestBrowserContainerIsLoading::destroyingViewAttachedToWindowDoesNotCrash()
{
    // Regression: closeSession → removeBrowserView → deleteLater used to crash because
    // ~QQuickItem emits windowChanged after BrowserView::m_impl was already destroyed.
    QQuickWindow window;
    window.resize(640, 480);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    BrowserContainer container;
    container.setParentItem(window.contentItem());
    container.setSize(QSizeF(640, 480));
    container.loadBrowserTab(QStringLiteral("tab-close"), QString(), true);

    BrowserView *view = BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-close"));
    QVERIFY(view);
    QVERIFY(view->window() == &window);

    container.enqueueBrowserViewRemovals({QStringLiteral("tab-close")});
    QTRY_VERIFY(BrowserContainerTestAccess::browserView(&container, QStringLiteral("tab-close")) == nullptr);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
}

QTEST_WIDGET_MAIN(TestBrowserContainerIsLoading)
#include "tst_browsercontainer_isloading.moc"
