#ifndef MAINCONTROLLER_H
#define MAINCONTROLLER_H

#include <QObject>
#include <qqml.h>
#include <QQmlApplicationEngine>
#include <QImage>
#include <QSet>
#include <QTimer>
#include <memory>

#include "inputmanager.h"
#include "buttoniconlayout.h"
#include "sessionmanager.h"
#include "website/websiterepository.h"
#include "website/websitesessioncatalog.h"
#include "website/websiteiconfetcher.h"
#include "history/historyrepository.h"


class BrowserContainer;
class BrowserController;
class PowerInhibitor;
class SettingsController;

class MainController : public QObject
{
    Q_OBJECT

public:
    explicit MainController(QObject *parent = nullptr);

    Q_PROPERTY(QString buttonIconLayout READ buttonIconLayout NOTIFY buttonIconLayoutChanged)
    QString buttonIconLayout() const;

    Q_INVOKABLE QString iconUrl(const QString &fileName) const;
    Q_INVOKABLE QString helpJoystickPhotoUrl() const;
    Q_INVOKABLE QString helpJoystickOverlayUrl() const;

    void setQmlEngine(QQmlApplicationEngine *qmlEngine) { m_qmlEngine = qmlEngine; }
    void setSettingsController(SettingsController *settingsController) { m_settingsController = settingsController; }

    void run();

    QImage getCursorShapeImage();

    InputManager *getInputManager() { return &m_inputManager; }
    WebsiteSessionCatalog *sessionCatalog() { return &m_sessionCatalog; }

    Q_INVOKABLE void bindBrowserContainer(QObject *containerObject);

    bool initializeWebsiteDatabase(const QString &dbFilePath = {});
    void initializeSessions();

    void onActiveBrowserUrlChanged(const QString &url, const QString &title);
    void onActiveBrowserLoadSucceeded(const QString &url, const QString &title);
    void onNewTabRequested(const QString &url);
    void beginPendingSessionSwitch(const QString &url);
    void clearPendingSessionSwitch();
    void notifyBrowserControllerAvailable(BrowserController *controller);

public:
    Q_INVOKABLE QString addWebsite(QString name, QString url);
    Q_INVOKABLE bool deleteWebsite(QString websiteId);
    Q_INVOKABLE bool editWebsite(QString websiteJsonString);
    Q_INVOKABLE QString getWebsiteList();
    Q_INVOKABLE QString getHomeWebsiteList() const;
    Q_INVOKABLE bool setWebsiteHasOpen(QString websiteId, bool hasOpen);

    // --- Session / Tab（HUD）---

    Q_INVOKABLE int sessionIndex() const;
    Q_INVOKABLE int tabIndex() const;
    Q_INVOKABLE QString focusLayer() const;
    Q_INVOKABLE bool moveSession(int delta);
    Q_INVOKABLE bool moveTab(int delta);
    Q_INVOKABLE bool setFocusLayer(const QString &layer);
    Q_INVOKABLE bool toggleFocusLayer();
    Q_INVOKABLE QString getSessionListJson() const;
    Q_INVOKABLE QString getTabListJson(const QString &sessionId) const;
    Q_INVOKABLE int activeTabIndex() const;
    Q_INVOKABLE int activeTabCount() const;
    Q_INVOKABLE QString openSite(const QString &url);
    Q_INVOKABLE QString openHistoryUrl(const QString &url);
    Q_INVOKABLE QString openSession(const QString &sessionId);
    Q_INVOKABLE QString openTab(const QString &tabId);
    Q_INVOKABLE QString confirmSelection();
    Q_INVOKABLE bool closeTab(const QString &tabId);
    Q_INVOKABLE bool closeSiteSession(const QString &siteId);

    Q_INVOKABLE QString getOpBarSetting(QString name);
    Q_INVOKABLE QString getHelpKeySetting();

    Q_INVOKABLE bool deleteHistory(QString itemId);
    Q_INVOKABLE QString getHistoryList();
    Q_INVOKABLE void recordHistoryVisit(const QString &title, const QString &url);
    void updateHistoryThumbnail(const QString &url, const QImage &image);
    void completeHistoryVisitUiUpdate(const QString &url);
    void clearAllHistory();

public slots:
    void handleNavigateRequested(const QString &url);
    void handleBrowserAction(const QString &action);
    void persistSessionsNow();

signals:
    void websiteListChange();
    void historyListChange();
    void selectionChanged();
    void buttonIconLayoutChanged();

private:
    struct WebsiteSessionIndex
    {
        QSet<QString> openSiteIds;
        QSet<QString> openMatchedWebsiteIds;
        bool hasActiveSession = false;
        QString activeSiteId;
        QString activeMatchedWebsiteId;
    };

    QString buildWebsiteListJson() const;
    WebsiteSessionIndex buildWebsiteSessionIndex() const;
    void syncButtonIconLayout();
    QString buttonIconImageRoot() const;
    QString fileUrlForRelativeIcon(const QString &relativePath) const;
    ButtonIconLayout::HelpJoystickPair currentHelpJoystickPair() const;
    QString buildHistoryListJson() const;
    bool websiteHasOpenSession(const Website &website, const WebsiteSessionIndex &index) const;
    bool websiteIsActiveSession(const Website &website, const WebsiteSessionIndex &index) const;
    void closeSessionForWebsiteUrl(const QString &url);
    bool shouldApplySessionUpdate(const QString &url) const;
    void navigateQmlToHome();
    void ensureBrowserBindings();
    QString normalizedWebsiteUrl(const QString &url) const;
    void loadActiveSessionTab(const QString &url, bool forceReload);
    void loadActiveSessionTabInBrowser(const QString &tabId, const QString &url, bool forceReload);
    void markBrowserLoadTiming(const QString &stage);
    void closeActiveBrowserPage();
    void leaveBrowserToHomeIfNeeded();
    void enqueueBrowserViewRemovalsForTabs(const QStringList &tabIds);
    void applyBrowserReadabilitySettings();
    QList<BrowserSession> orderedOpenSessions() const;
    void clampSessionIndex();
    void clampTabIndex();
    QString selectedSessionId() const;
    QString buildSessionListJson() const;
    QString buildTabListJson(const QString &sessionId) const;
    QString thumbnailUrlForTab(const BrowserTab &tab) const;
    bool sessionIsTemporary(const BrowserSession &session) const;
    int tabCountForSession(const BrowserSession &session) const;
    void syncSelectionToActiveSession();
    void restorePersistedSessions();
    void scheduleSessionPersist();
    void maybeFetchIconForTemporarySession(const QString &sessionId);
    void onWebsiteIconFetched(const QString &websiteId, const QString &fileUrl);
    void updatePlaybackInhibit();

    QQmlApplicationEngine *m_qmlEngine = nullptr;

    InputManager m_inputManager;
    WebsiteRepository m_repository;
    HistoryRepository m_historyRepository;
    WebsiteSessionCatalog m_sessionCatalog;
    WebsiteIconFetcher m_iconFetcher;
    std::unique_ptr<SessionManager> m_sessionManager;
    SettingsController *m_settingsController = nullptr;
    BrowserContainer *m_browserContainer = nullptr;
    BrowserController *m_browserController = nullptr;
    PowerInhibitor *m_powerInhibitor = nullptr;
    QSet<QString> m_pendingRevisitHistoryRefresh;
    QString m_pendingSessionSwitchSiteId;
    QString m_pendingNavigateUrl;
    bool m_browserNavigateConnected = false;
    bool m_browserActionConnected = false;
    bool m_browserHomeVisibleConnected = false;
    bool m_playbackAppStateConnected = false;
    bool m_lastPlaybackInhibitActive = false;
    QString m_lastPlaybackInhibitReason;
    int m_sessionIndex = 0;
    int m_tabIndex = 0;
    QString m_focusLayer = QStringLiteral("session");
    bool m_sessionPersistScheduled = false;
    QString m_buttonIconLayout = QStringLiteral("xbox");
};

#endif // MAINCONTROLLER_H
