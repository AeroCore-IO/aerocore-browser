#pragma once

#include <QHash>
#include <QMainWindow>

class BookmarkModel;
class BrowserController;
class BrowserView;
class HistoryModel;
class InputManager;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class PowerInhibitor;
class QPushButton;
class QTimer;
class SessionManager;
class QStackedWidget;
class SiteConfigManager;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    MainWindow(BrowserController *browserController,
               BookmarkModel *bookmarkModel,
               HistoryModel *historyModel,
               InputManager *inputManager,
               SiteConfigManager *siteConfig,
               bool startWindowed,
               const QString &initialUrl,
               QWidget *parent = nullptr);
    SiteConfigManager *siteConfigManager() const { return m_siteConfig; }

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void moveEvent(QMoveEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    int scaledPx(int value) const;
    int activeArrowCursorSize() const;
    void applyPointerCursor(bool force = false);
    void notePointerActivity();
    void handlePointerIdleTimeout();
    void updateCustomCursorOverlay(bool forceRaise = false);
    QListWidgetItem *makeListItem(const QString &title, const QString &detail) const;
    void applyAppUiScale();
    void buildHomePage();
    void buildBrowserPage();
    void connectSignals();
    void refreshBookmarkList();
    void refreshSessionList();
    void openBookmarkDialog();
    void openAddressDialog();
    void openHistoryDialog();
    void openSettingsDialog();
    void buildAddressOverlay();
    void closeAddressOverlay();
    void refreshBookmarkOverlayList();
    void buildBookmarkOverlay();
    void closeBookmarkOverlay();
    void refreshHistoryOverlayList();
    void buildHistoryOverlay();
    void closeHistoryOverlay();
    void buildSettingsOverlay();
    void closeSettingsOverlay();
    void buildExitOverlay();
    void closeExitOverlay();
    void restoreFocusAfterOverlayClose();
    void updateSettingsOverlayGeometry();
    void updateAddressOverlayGeometry();
    void updateBookmarkOverlayGeometry();
    void updateHistoryOverlayGeometry();
    void updateExitOverlayGeometry();
    void activateHighlightedHomeItem();
    void openSite(const QString &url);
    void openSession(const QString &sessionId);
    void openUrlInNewSession(const QString &url, const QString &title = QString());
    void showBookmarksPage();
    void beginPendingSessionSwitch(const QString &url);
    void clearPendingSessionSwitch();
    BrowserView *ensureBrowserView(const QString &tabId);
    void enqueueBrowserViewRemovals(const QStringList &tabIds);
    void processPendingBrowserViewRemoval();
    void removeBrowserView(const QString &tabId);
    void showBrowserTab(const QString &tabId);
    void loadBrowserTab(const QString &tabId, const QString &url, bool forceReload);
    void applyBrowserReadability(BrowserView *view, const QString &url);
    void syncBrowserControllerFromView(BrowserView *view);
    bool shouldApplySessionUpdate(const QString &url);
    bool navigateActiveDialog(int key);
    bool activateFocusedDialogControl();
    void handleAction(const QString &action);
    void handleBackAction();
    void closeSiteSession(const QString &siteId);
    void showHudTemporarily(int durationMs);
    void cancelTemporaryHudHide();
    void adjustPageZoom(double delta);
    void adjustMinimumFontSize(int delta);
    int bookmarkIndexForUrl(const QString &url) const;
    void syncBookmarkCursor(const QString &url);
    QString selectedBookmarkUrl() const;
    QWidget *normalizedHomeFocusWidget() const;
    QPushButton *currentHomeTopButton() const;
    QList<QWidget *> orderedDialogFocusWidgets(QWidget *dialog) const;
    bool navigateOrderedDialogFocus(QWidget *dialog, int key);
    bool toggleHomeFocusSection();
    bool handleHomeNavigationKey(int key);
    void updateHomeSectionVisualState();
    void rememberHomeTopButtonFocus(QWidget *focus = nullptr);
    bool restoreRememberedHomeTopButtonFocus();
    void restoreHomeFocus();
    void updateHud();
    void updatePointerMode();
    void updatePlaybackInhibit();
    void updateTouchDragIndicator();
    bool dispatchNavigationKey(int key);
    bool activatePrimaryAction();
    QString semanticActionForKey(int key) const;

    BrowserController *m_browserController = nullptr;
    BookmarkModel *m_bookmarkModel = nullptr;
    HistoryModel *m_historyModel = nullptr;
    InputManager *m_inputManager = nullptr;
    SessionManager *m_sessionManager = nullptr;
    SiteConfigManager *m_siteConfig = nullptr;
    QStackedWidget *m_stack = nullptr;
    QWidget *m_homePage = nullptr;
    QWidget *m_browserPage = nullptr;
    QStackedWidget *m_browserStack = nullptr;
    BrowserView *m_browserView = nullptr;
    QWidget *m_hud = nullptr;
    QWidget *m_cursorOverlay = nullptr;
    QWidget *m_touchDragIndicator = nullptr;
    QWidget *m_addressOverlay = nullptr;
    QWidget *m_addressPanel = nullptr;
    QWidget *m_bookmarkOverlay = nullptr;
    QWidget *m_bookmarkPanel = nullptr;
    QWidget *m_historyOverlay = nullptr;
    QWidget *m_historyPanel = nullptr;
    QWidget *m_settingsOverlay = nullptr;
    QWidget *m_settingsPanel = nullptr;
    QWidget *m_exitOverlay = nullptr;
    QWidget *m_exitPanel = nullptr;
    PowerInhibitor *m_powerInhibitor = nullptr;
    QLabel *m_settingsSystemLabel = nullptr;
    QLabel *m_exitMessageLabel = nullptr;
    QLabel *m_homeDebugLabel = nullptr;
    QLabel *m_titleLabel = nullptr;
    QLabel *m_urlLabel = nullptr;
    QLabel *m_modeLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_tabListLabel = nullptr;
    QLabel *m_sessionListLabel = nullptr;
    QPushButton *m_homeAddressButton = nullptr;
    QPushButton *m_homeHistoryButton = nullptr;
    QPushButton *m_homeSettingsButton = nullptr;
    QListWidget *m_bookmarkList = nullptr;
    QListWidget *m_recentList = nullptr;
    QLineEdit *m_addressLineEdit = nullptr;
    QPushButton *m_addressOpenButton = nullptr;
    QPushButton *m_addressCancelButton = nullptr;
    QListWidget *m_bookmarkDialogList = nullptr;
    QPushButton *m_bookmarkToggleButton = nullptr;
    QPushButton *m_bookmarkCloseButton = nullptr;
    QListWidget *m_historyDialogList = nullptr;
    QPushButton *m_historyCloseButton = nullptr;
    QPushButton *m_settingsDefaultButton = nullptr;
    QList<QWidget *> m_settingsNavigationWidgets;
    QPushButton *m_exitConfirmButton = nullptr;
    QPushButton *m_exitCancelButton = nullptr;
    int m_bookmarkCursor = 0;
    QString m_activeBrowserTabId;
    QHash<QString, BrowserView *> m_browserViews;
    QStringList m_pendingBrowserViewRemovals;
    bool m_browserViewRemovalScheduled = false;
    QString m_pendingSessionSwitchSiteId;
    int m_hudAutoHideGeneration = 0;
    int m_homeBookmarkReentryRow = -1;
    int m_homeRecentReentryRow = -1;
    int m_lastHomeTopButtonIndex = 0;
    QPushButton *m_pendingHomeOverlayReturnButton = nullptr;
    QTimer *m_pointerIdleTimer = nullptr;
    int m_appliedArrowCursorSize = -1;
    bool m_lastPlaybackInhibitActive = false;
    QString m_lastPlaybackInhibitReason;
    bool m_pointerIdleHidden = false;
    bool m_pointerCursorHidden = false;
};