#include "maincontroller.h"
#include "buttoniconlayout.h"
#include "browsercontroller.h"
#include "database/databasemanager.h"
#include "history/historyitem.h"
#include "history/historythumbnailstorage.h"
#include "setting/settingscontroller.h"
#include "website/websitevalidator.h"
#include "cursorutils.h"
#include <QDateTime>
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QUrl>

#include "sessionpersistence.h"

#include <optional>

namespace {
[[maybe_unused]] const char *const kUiConfigTranslationStrings[] = {
    QT_TRANSLATE_NOOP("OpBar", "Switch top menu"),
    QT_TRANSLATE_NOOP("OpBar", "Switch websites"),
    QT_TRANSLATE_NOOP("OpBar", "Confirm"),
    QT_TRANSLATE_NOOP("OpBar", "Cancel or return to session"),
    QT_TRANSLATE_NOOP("OpBar", "Move between fields"),
    QT_TRANSLATE_NOOP("OpBar", "Navigate sharing options"),
    QT_TRANSLATE_NOOP("OpBar", "Navigate browsing history"),
    QT_TRANSLATE_NOOP("OpBar", "Navigate settings"),
    QT_TRANSLATE_NOOP("OpBar", "Menu"),
    QT_TRANSLATE_NOOP("OpBar", "Open"),
    QT_TRANSLATE_NOOP("OpBar", "Back"),
    QT_TRANSLATE_NOOP("OpBar", "Select"),
    QT_TRANSLATE_NOOP("OpBar", "Close tab"),
    QT_TRANSLATE_NOOP("OpBar", "Close website session"),
    QT_TRANSLATE_NOOP("OpBar", "Hide HUD"),
    QT_TRANSLATE_NOOP("HelpKeys", "Click"),
    QT_TRANSLATE_NOOP("HelpKeys", "Click page content."),
    QT_TRANSLATE_NOOP("HelpKeys", "Back"),
    QT_TRANSLATE_NOOP("HelpKeys", "Short press: hide HUD, go back, close tab, or go home. Long press to close the current website."),
    QT_TRANSLATE_NOOP("HelpKeys", "Close tab"),
    QT_TRANSLATE_NOOP("HelpKeys", "Close the current tab; if it is the last tab, switch websites or go home."),
    QT_TRANSLATE_NOOP("HelpKeys", "Refresh"),
    QT_TRANSLATE_NOOP("HelpKeys", "Refresh the current page."),
    QT_TRANSLATE_NOOP("HelpKeys", "LB+A: Drag"),
    QT_TRANSLATE_NOOP("HelpKeys", "Hold to drag with the left stick; press A again to release."),
    QT_TRANSLATE_NOOP("HelpKeys", "RB+A: Right-click"),
    QT_TRANSLATE_NOOP("HelpKeys", "Open the context menu at the cursor."),
    QT_TRANSLATE_NOOP("HelpKeys", "LB+D-pad: Zoom and font size"),
    QT_TRANSLATE_NOOP("HelpKeys", "Zoom the page with up/down; adjust minimum font size with left/right."),
    QT_TRANSLATE_NOOP("HelpKeys", "LT+D-pad: Back / forward"),
    QT_TRANSLATE_NOOP("HelpKeys", "Left to go back; right to go forward."),
    QT_TRANSLATE_NOOP("HelpKeys", "LT/RT+stick: Precision / speed"),
    QT_TRANSLATE_NOOP("HelpKeys", "Hold LT for slower movement or scrolling; hold RT to move faster.")
};
}

MainController::MainController(QObject *parent)
    : QObject{parent}
{
    connect(&m_iconFetcher, &WebsiteIconFetcher::iconFetched,
            this, &MainController::onWebsiteIconFetched);

    connect(&m_inputManager, &InputManager::resolvedButtonIconLayoutChanged,
            this, &MainController::syncButtonIconLayout);
    connect(&m_inputManager, &InputManager::connectedGamepadChanged,
            this, &MainController::syncButtonIconLayout);
    syncButtonIconLayout();
}

void MainController::syncButtonIconLayout()
{
    const QString layout = m_inputManager.resolvedButtonIconLayout();
    if (layout != m_buttonIconLayout) {
        m_buttonIconLayout = layout;
        emit buttonIconLayoutChanged();
    }
}

QString MainController::buttonIconLayout() const
{
    return m_buttonIconLayout;
}

QString MainController::buttonIconImageRoot() const
{
    return QGuiApplication::applicationDirPath() + QStringLiteral("/config/ui/image/");
}

QString MainController::fileUrlForRelativeIcon(const QString &relativePath) const
{
    if (relativePath.isEmpty()) {
        return {};
    }
    return QUrl::fromLocalFile(buttonIconImageRoot() + relativePath).toString();
}

QString MainController::iconUrl(const QString &fileName) const
{
    const QString relative = ButtonIconLayout::resolveIconRelativePath(
        m_buttonIconLayout, buttonIconImageRoot(), fileName);
    return fileUrlForRelativeIcon(relative);
}

ButtonIconLayout::HelpJoystickPair MainController::currentHelpJoystickPair() const
{
    return ButtonIconLayout::resolveHelpJoystickPair(m_buttonIconLayout, buttonIconImageRoot());
}

QString MainController::helpJoystickPhotoUrl() const
{
    return fileUrlForRelativeIcon(currentHelpJoystickPair().photoRelativePath);
}

QString MainController::helpJoystickOverlayUrl() const
{
    return fileUrlForRelativeIcon(currentHelpJoystickPair().overlayRelativePath);
}

void MainController::run()
{
    if (!DatabaseManager::isInitialized()) {
        initializeWebsiteDatabase();
    }
    m_repository.importPresetsIfEmpty();
    initializeSessions();
}

bool MainController::initializeWebsiteDatabase(const QString &dbFilePath)
{
    return DatabaseManager::initialize(dbFilePath);
}

QImage MainController::getCursorShapeImage()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QPixmap pixmap = adaptiveArrowCursorPixmapForScreen(screen);
    return pixmap.toImage();
}

QString MainController::buildWebsiteListJson() const
{
    QJsonArray websiteJsonArray;
    const QList<Website> websites = m_repository.listAll();
    const WebsiteSessionIndex index = buildWebsiteSessionIndex();
    for (const Website &website : websites) {
        Website output = website;
        output.hasOpen = websiteHasOpenSession(website, index);
        output.isCurrent = websiteIsActiveSession(website, index);
        websiteJsonArray.append(output.toJsonObject());
    }

    return QJsonDocument(websiteJsonArray).toJson(QJsonDocument::Compact);
}

MainController::WebsiteSessionIndex MainController::buildWebsiteSessionIndex() const
{
    WebsiteSessionIndex index;
    if (!m_sessionManager) {
        return index;
    }

    const QList<BrowserSession> sessions = m_sessionManager->allSessions();
    for (const BrowserSession &session : sessions) {
        if (!session.siteId.isEmpty()) {
            index.openSiteIds.insert(session.siteId);
        }
        if (session.currentUrl.isEmpty()) {
            continue;
        }
        const std::optional<Website> match = m_repository.findMatchingForUrl(session.currentUrl);
        if (match.has_value()) {
            index.openMatchedWebsiteIds.insert(match->id);
        }
    }

    index.hasActiveSession = !m_sessionManager->activeSessionId().isEmpty();
    if (!index.hasActiveSession) {
        return index;
    }

    index.activeSiteId = m_sessionManager->activeSiteId();
    const QString activeUrl = m_sessionManager->activeSessionUrl();
    if (!activeUrl.isEmpty()) {
        const std::optional<Website> match = m_repository.findMatchingForUrl(activeUrl);
        if (match.has_value()) {
            index.activeMatchedWebsiteId = match->id;
        }
    }
    return index;
}

QString MainController::getWebsiteList()
{
    if (!DatabaseManager::isInitialized()) {
        return QStringLiteral("[]");
    }
    return buildWebsiteListJson();
}

QString MainController::getHomeWebsiteList() const
{
    return buildWebsiteListJson();
}

QString MainController::getOpBarSetting(QString name)
{
    static QJsonObject root;
    if (root.isEmpty())
    {
        QString settingFilePath = QGuiApplication::applicationDirPath()+"/config/ui/opbarsetting.json";
        QFile file(settingFilePath);
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray jsonData = file.readAll();
            file.close();

            QJsonDocument jsonDocument = QJsonDocument::fromJson(jsonData);
            root = jsonDocument.object();
        }
        else
        {
            qWarning() << "Failed to open op bar config:" << settingFilePath;
        }
    }

    if (!root.contains(name))
    {
        return "[]";
    }

    const QString imageFolderPath = buttonIconImageRoot();

    QJsonArray opBarArray = root[name].toArray();
    for (int i = 0; i < opBarArray.size(); ++i)
    {
        QJsonArray keyArray = opBarArray[i].toArray();
        for (int j = 0; j < keyArray.size(); ++j)
        {
            QJsonObject element = keyArray[j].toObject();
            const QString type = element["type"].toString();
            if (type == "image")
            {
                const QString imageFileName = ButtonIconLayout::resolveIconRelativePath(
                    m_buttonIconLayout, imageFolderPath, element["value"].toString());
                element["value"] = QUrl::fromLocalFile(imageFolderPath + imageFileName).toString();
                keyArray[j] = element;
            }
            else if (type == "text")
            {
                const QString sourceText = ButtonIconLayout::adaptHelpText(
                    m_buttonIconLayout, element["value"].toString());
                element["value"] = QCoreApplication::translate(
                    "OpBar", sourceText.toUtf8().constData());
                keyArray[j] = element;
            }
        }
        opBarArray[i] = keyArray;
    }

    QJsonDocument doc(opBarArray);
    return doc.toJson(QJsonDocument::Compact);
}

QString MainController::getHelpKeySetting()
{
    static QJsonArray root;
    if (root.isEmpty())
    {
        QString settingFilePath = QGuiApplication::applicationDirPath()+"/config/ui/key_help.json";
        QFile file(settingFilePath);
        if (file.open(QIODevice::ReadOnly))
        {
            QByteArray jsonData = file.readAll();
            file.close();

            QJsonDocument jsonDocument = QJsonDocument::fromJson(jsonData);
            root = jsonDocument.array();
        }
        else
        {
            qWarning() << "Failed to open help key config:" << settingFilePath;
        }
    }

    const QString imageFolderPath = buttonIconImageRoot();

    QJsonArray result = root;
    for (int i = 0; i < result.size(); ++i)
    {
        QJsonObject keyElement = result[i].toObject();
        QJsonArray imageArray = keyElement["image"].toArray();
        for (int j=0; j<imageArray.size(); j++)
        {
            const QString token = imageArray[j].toString();
            if (token == QLatin1String("+") || token == QLatin1String("/")) {
                continue;
            }
            const QString imageFileName = ButtonIconLayout::resolveIconRelativePath(
                m_buttonIconLayout, imageFolderPath, token);
            imageArray[j] = QUrl::fromLocalFile(imageFolderPath + imageFileName).toString();
        }
        keyElement["image"] = imageArray;
        const QString sourceTitle = keyElement["title"].toString();
        const QString sourceDescription = keyElement["description"].toString();
        const QString translatedTitle = QCoreApplication::translate(
            "HelpKeys", sourceTitle.toUtf8().constData());
        const QString translatedDescription = QCoreApplication::translate(
            "HelpKeys", sourceDescription.toUtf8().constData());
        keyElement["title"] = ButtonIconLayout::adaptHelpText(
            m_buttonIconLayout, translatedTitle);
        keyElement["description"] = ButtonIconLayout::adaptHelpText(
            m_buttonIconLayout, translatedDescription);
        result[i] = keyElement;
    }

    QJsonDocument doc(result);
    return doc.toJson(QJsonDocument::Compact);
}

QString MainController::addWebsite(QString name, QString url)
{
    const ValidationResult validation = WebsiteValidator::validateAddParams(name, url);
    if (!validation.ok) {
        return validation.error.isEmpty() ? QStringLiteral("添加网站失败") : validation.error;
    }

    if (m_repository.urlExists(validation.normalizedUrl)) {
        return QStringLiteral("该网站已存在");
    }

    if (!m_repository.add(validation.trimmedName, validation.normalizedUrl)) {
        return QStringLiteral("添加网站失败");
    }

    m_sessionCatalog.refresh();
    emit websiteListChange();

    const std::optional<Website> added = m_repository.findByUrl(validation.normalizedUrl);
    if (added.has_value() && added->icon.isEmpty()) {
        m_iconFetcher.fetchForWebsite(added->id, validation.normalizedUrl);
    }

    return {};
}

bool MainController::deleteWebsite(QString websiteId)
{
    const QString id = websiteId.trimmed();
    if (!WebsiteValidator::validateId(id)) {
        emit websiteListChange();
        return false;
    }

    if (!m_repository.exists(id)) {
        emit websiteListChange();
        return false;
    }

    const std::optional<Website> existingWebsite = m_repository.findById(id);
    const QList<BrowsingHistoryItem> removedHistory =
        m_historyRepository.removeAssociatedWithWebsite(id, m_repository);
    for (const BrowsingHistoryItem &item : removedHistory) {
        HistoryThumbnailStorage::removeThumbnail(item.id);
        m_pendingRevisitHistoryRefresh.remove(normalizeHistoryUrl(item.url));
    }

    const bool removed = m_repository.remove(id);
    if (removed) {
        if (existingWebsite.has_value()) {
            closeSessionForWebsiteUrl(existingWebsite->url);
        }
        m_sessionCatalog.refresh();
    }

    emit websiteListChange();
    if (!removedHistory.isEmpty()) {
        emit historyListChange();
    }
    return removed;
}

bool MainController::editWebsite(QString websiteJsonString)
{
    const ValidationResult validation = WebsiteValidator::validateEditParams(websiteJsonString);
    if (!validation.ok) {
        return false;
    }

    const std::optional<Website> existing = m_repository.findById(validation.id);
    if (!existing.has_value()) {
        return false;
    }

    Website updated = existing.value();
    if (validation.hasNameField) {
        updated.name = validation.trimmedName;
    }
    if (validation.hasUrlField) {
        if (m_repository.urlExists(validation.normalizedUrl, validation.id)) {
            return false;
        }
        const QString oldNormalized = normalizedWebsiteUrl(existing->url);
        if (oldNormalized != validation.normalizedUrl) {
            closeSessionForWebsiteUrl(existing->url);
        }
        updated.url = validation.normalizedUrl;
    }
    if (validation.hasIconField) {
        updated.icon = validation.icon;
    }

    if (!m_repository.update(updated)) {
        return false;
    }

    m_sessionCatalog.refresh();
    emit websiteListChange();
    return true;
}

bool MainController::setWebsiteHasOpen(QString websiteId, bool hasOpen)
{
    Q_UNUSED(hasOpen);

    const QString id = websiteId.trimmed();
    if (!WebsiteValidator::validateId(id) || !m_repository.exists(id)) {
        return false;
    }

    emit websiteListChange();
    return true;
}

void MainController::maybeFetchIconForTemporarySession(const QString &sessionId)
{
    if (!m_sessionManager || sessionId.isEmpty()) {
        return;
    }

    std::optional<BrowserSession> matched;
    for (const BrowserSession &session : m_sessionManager->allSessions()) {
        if (session.sessionId == sessionId) {
            matched = session;
            break;
        }
    }

    if (!matched.has_value() || !matched->icon.isEmpty() || !sessionIsTemporary(*matched)) {
        return;
    }

    QString fetchUrl = matched->currentUrl.trimmed();
    if (fetchUrl.isEmpty()) {
        const QUrl asUrl(matched->siteId);
        if (asUrl.isValid() && !asUrl.scheme().isEmpty()) {
            fetchUrl = matched->siteId;
        } else if (!matched->siteId.isEmpty()) {
            fetchUrl = QStringLiteral("https://%1").arg(matched->siteId);
        }
    }

    if (fetchUrl.isEmpty() || matched->siteId.isEmpty()) {
        return;
    }

    m_iconFetcher.fetchForWebsite(matched->siteId, fetchUrl);
}

void MainController::onWebsiteIconFetched(const QString &websiteId, const QString &fileUrl)
{
    const std::optional<Website> existing = m_repository.findById(websiteId);
    if (existing.has_value()) {
        if (!m_repository.updateIconIfEmpty(websiteId, fileUrl)) {
            return;
        }

        m_sessionCatalog.refresh();
        if (m_sessionManager) {
            m_sessionManager->refreshIconForSiteId(existing->url, fileUrl);
        }

        emit websiteListChange();
        emit selectionChanged();
        return;
    }

    if (m_sessionManager) {
        m_sessionManager->refreshIconForSiteId(websiteId, fileUrl);
    }
}

QString MainController::buildHistoryListJson() const
{
    QHash<QString, Website> websiteById;
    for (const Website &website : m_repository.listAll()) {
        websiteById.insert(website.id, website);
    }

    QJsonArray historyJsonArray;
    const QList<BrowsingHistoryItem> items = m_historyRepository.listAll();
    for (const BrowsingHistoryItem &item : items) {
        historyJsonArray.append(item.toJsonObject());
    }

    struct WebsiteInfo {
        QString name;
        QString icon;
    };
    QMap<QString, WebsiteInfo> websiteInfoMap;
    QMap<QString, QList<QJsonObject>> groupedItems;

    for (int i = 0; i < historyJsonArray.size(); ++i) {
        QJsonObject item = historyJsonArray[i].toObject();
        QString websiteId = item["website_id"].toString();
        QString websiteName = item["website_name"].toString();
        QString websiteIcon;
        if (!websiteId.isEmpty()) {
            const auto it = websiteById.constFind(websiteId);
            if (it != websiteById.constEnd()) {
                websiteName = it->name;
                websiteIcon = it->icon;
            }
        }

        if (!websiteInfoMap.contains(websiteId)) {
            WebsiteInfo info;
            info.name = websiteName;
            info.icon = websiteIcon;
            websiteInfoMap[websiteId] = info;
        }

        QString isoTime = item["last_visit_time"].toString();
        QDateTime dateTime = QDateTime::fromString(isoTime, Qt::ISODate);
        if (dateTime.isValid()) {
            item["last_visit_time"] = dateTime.toSecsSinceEpoch();
        }

        const QString updatedIso = item[QStringLiteral("updated_at")].toString();
        const QDateTime updatedAt = QDateTime::fromString(updatedIso, Qt::ISODate);
        if (updatedAt.isValid()) {
            item[QStringLiteral("updated_at")] = updatedAt.toSecsSinceEpoch();
        }

        groupedItems[websiteId].append(item);
    }

    QJsonArray resultArray;
    QMapIterator<QString, QList<QJsonObject>> it(groupedItems);
    while (it.hasNext()) {
        it.next();
        QString websiteId = it.key();
        WebsiteInfo info = websiteInfoMap[websiteId];

        QList<QJsonObject> sortedList = it.value();
        std::sort(sortedList.begin(), sortedList.end(), [](const QJsonObject &a, const QJsonObject &b) {
            qint64 timeA = a["last_visit_time"].toInteger();
            qint64 timeB = b["last_visit_time"].toInteger();
            return timeA > timeB;
        });

        QJsonArray sortedItems;
        for (const QJsonObject &obj : sortedList) {
            sortedItems.append(obj);
        }

        QJsonObject siteObj;
        siteObj["id"] = websiteId;
        siteObj["name"] = info.name;
        siteObj["icon"] = info.icon;
        siteObj["items"] = sortedItems;

        resultArray.append(siteObj);
    }

    return QJsonDocument(resultArray).toJson(QJsonDocument::Compact);
}

QString MainController::getHistoryList()
{
    if (!DatabaseManager::isInitialized()) {
        return QStringLiteral("[]");
    }
    return buildHistoryListJson();
}

bool MainController::deleteHistory(QString itemId)
{
    const QString id = itemId.trimmed();
    if (!WebsiteValidator::validateId(id)) {
        emit historyListChange();
        return false;
    }

    if (!m_historyRepository.exists(id)) {
        emit historyListChange();
        return false;
    }

    const bool removed = m_historyRepository.remove(id);
    if (removed) {
        HistoryThumbnailStorage::removeThumbnail(id);
    }
    emit historyListChange();
    return removed;
}

void MainController::clearAllHistory()
{
    if (!DatabaseManager::isInitialized()) {
        return;
    }

    if (!HistoryThumbnailStorage::clearAll()) {
        return;
    }

    if (!m_historyRepository.clearAll()) {
        return;
    }

    m_pendingRevisitHistoryRefresh.clear();
    emit historyListChange();
}

void MainController::recordHistoryVisit(const QString &title, const QString &url)
{
    if (!DatabaseManager::isInitialized()) {
        return;
    }

    const QString normalizedUrl = normalizeHistoryUrl(url);
    const bool isRevisit = !normalizedUrl.isEmpty()
        && m_historyRepository.itemForUrl(normalizedUrl).has_value();

    if (!m_historyRepository.upsertVisit(title, url)) {
        return;
    }

    if (isRevisit) {
        m_pendingRevisitHistoryRefresh.insert(normalizedUrl);
        return;
    }

    emit historyListChange();
}

void MainController::updateHistoryThumbnail(const QString &url, const QImage &image)
{
    if (!DatabaseManager::isInitialized() || image.isNull()) {
        return;
    }

    const QString normalizedUrl = normalizeHistoryUrl(url);
    if (normalizedUrl.isEmpty()) {
        return;
    }

    const std::optional<BrowsingHistoryItem> item = m_historyRepository.itemForUrl(normalizedUrl);
    if (!item) {
        return;
    }

    const QString fileUrl = HistoryThumbnailStorage::saveThumbnail(item->id, image);
    if (fileUrl.isEmpty()) {
        return;
    }

    bool updated = false;
    if (item->thumbnail.isEmpty()) {
        updated = m_historyRepository.setThumbnailIfEmpty(item->id, fileUrl);
    } else {
        updated = m_historyRepository.touchUpdatedAt(item->id);
    }

    if (updated) {
        m_pendingRevisitHistoryRefresh.remove(normalizedUrl);
        emit historyListChange();
    }
}

void MainController::completeHistoryVisitUiUpdate(const QString &url)
{
    const QString normalizedUrl = normalizeHistoryUrl(url);
    if (normalizedUrl.isEmpty()) {
        return;
    }

    if (!m_pendingRevisitHistoryRefresh.remove(normalizedUrl)) {
        return;
    }

    emit historyListChange();
}

void MainController::initializeSessions()
{
    m_sessionCatalog.setRepository(&m_repository);
    m_sessionCatalog.refresh();
    if (!m_sessionManager) {
        m_sessionManager = std::make_unique<SessionManager>(&m_sessionCatalog, this);
        connect(m_sessionManager.get(), &SessionManager::sessionsChanged, this, [this]() {
            clampSessionIndex();
            clampTabIndex();
            emit websiteListChange();
            emit selectionChanged();
            scheduleSessionPersist();
        });
        connect(m_sessionManager.get(), &SessionManager::activeSessionChanged, this, [this]() {
            syncSelectionToActiveSession();
            emit websiteListChange();
            scheduleSessionPersist();
        });
        restorePersistedSessions();
    }
}

void MainController::restorePersistedSessions()
{
    if (!m_sessionManager) {
        return;
    }

    QString persistedActiveSessionId;
    const QList<BrowserSession> persistedSessions = SessionPersistence::load(&persistedActiveSessionId);
    if (persistedSessions.isEmpty()) {
        return;
    }

    m_sessionManager->restoreSessions(persistedSessions, persistedActiveSessionId);
    syncSelectionToActiveSession();
}

void MainController::scheduleSessionPersist()
{
    if (m_sessionPersistScheduled) {
        return;
    }

    m_sessionPersistScheduled = true;
    QTimer::singleShot(500, this, [this]() {
        m_sessionPersistScheduled = false;
        persistSessionsNow();
    });
}

void MainController::persistSessionsNow()
{
    if (!m_sessionManager) {
        return;
    }

    SessionPersistence::save(m_sessionManager->allSessions(), m_sessionManager->activeSessionId());
}

QString MainController::normalizedWebsiteUrl(const QString &url) const
{
    const QUrl normalized = QUrl::fromUserInput(url.trimmed());
    return normalized.isValid() ? normalized.toString(QUrl::RemoveFragment) : QString();
}

bool MainController::websiteHasOpenSession(const Website &website, const WebsiteSessionIndex &index) const
{
    if (!m_sessionManager) {
        return false;
    }

    const QString normalized = normalizedWebsiteUrl(website.url);
    if (!normalized.isEmpty() && index.openSiteIds.contains(normalized)) {
        return true;
    }

    const QString host = QUrl(normalized).host().toLower();
    if (!host.isEmpty() && index.openSiteIds.contains(host)) {
        return true;
    }

    return index.openMatchedWebsiteIds.contains(website.id);
}

bool MainController::websiteIsActiveSession(const Website &website, const WebsiteSessionIndex &index) const
{
    if (!index.hasActiveSession) {
        return false;
    }

    const QString normalized = normalizedWebsiteUrl(website.url);
    if (!normalized.isEmpty() && index.activeSiteId == normalized) {
        return true;
    }

    const QString host = QUrl(normalized).host().toLower();
    if (!host.isEmpty() && index.activeSiteId == host) {
        return true;
    }

    return !index.activeMatchedWebsiteId.isEmpty() && index.activeMatchedWebsiteId == website.id;
}

void MainController::closeSessionForWebsiteUrl(const QString &url)
{
    if (!m_sessionManager) {
        return;
    }

    const QString normalized = normalizedWebsiteUrl(url);
    if (normalized.isEmpty()) {
        return;
    }

    m_sessionManager->closeSessionForSite(normalized);
    const QString host = QUrl(normalized).host().toLower();
    if (!host.isEmpty()) {
        m_sessionManager->closeSessionForSite(host);
    }
}

void MainController::applyBrowserReadabilitySettings()
{
    if (m_settingsController) {
        m_settingsController->applyToCurrentUrl();
    }
}

void MainController::beginPendingSessionSwitch(const QString &url)
{
    if (!m_sessionManager) {
        return;
    }

    m_pendingSessionSwitchSiteId = m_sessionManager->siteIdForUrl(url);
}

void MainController::clearPendingSessionSwitch()
{
    m_pendingSessionSwitchSiteId.clear();
}

bool MainController::shouldApplySessionUpdate(const QString &url) const
{
    if (m_pendingSessionSwitchSiteId.isEmpty() || !m_sessionManager) {
        return true;
    }

    const QString siteId = m_sessionManager->siteIdForUrl(url);
    if (siteId.isEmpty() || siteId != m_pendingSessionSwitchSiteId) {
        return false;
    }

    return true;
}

void MainController::onActiveBrowserUrlChanged(const QString &url, const QString &title)
{
    if (!m_sessionManager || !shouldApplySessionUpdate(url)) {
        return;
    }

    m_sessionManager->updateActiveSession(url, title);
    applyBrowserReadabilitySettings();
}

void MainController::onActiveBrowserLoadSucceeded(const QString &url, const QString &title)
{
    if (!m_sessionManager) {
        return;
    }

    if (shouldApplySessionUpdate(url)) {
        m_sessionManager->updateActiveSession(url, title);
        clearPendingSessionSwitch();
        applyBrowserReadabilitySettings();
    }
    emit websiteListChange();
}

void MainController::loadActiveSessionTab(const QString &url, bool forceReload)
{
    if (!m_sessionManager || !m_browserContainer) {
        return;
    }

    const QString tabId = m_sessionManager->activeTabId();
    if (tabId.isEmpty()) {
        return;
    }

    loadActiveSessionTabInBrowser(tabId, url, forceReload);
}

void MainController::onNewTabRequested(const QString &url)
{
    if (!m_sessionManager) {
        return;
    }

    const QString tabUrl = m_sessionManager->openUrlInNewTab(url, url);
    if (tabUrl.isEmpty()) {
        return;
    }

    loadActiveSessionTab(tabUrl, true);
}

void MainController::handleNavigateRequested(const QString &url)
{
    if (!m_sessionManager) {
        return;
    }

    if (!m_browserContainer) {
        m_pendingNavigateUrl = url;
        return;
    }

    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    markBrowserLoadTiming(QStringLiteral("handleNavigateRequested enter"));

    const QString sessionId = m_sessionManager->ensureSiteSession(url);
    if (sessionId.isEmpty() || m_sessionManager->activateSession(sessionId).isEmpty()) {
        return;
    }

    m_sessionManager->updateActiveSession(url);
    maybeFetchIconForTemporarySession(m_sessionManager->activeSessionId());
    loadActiveSessionTab(url, true);
    syncSelectionToActiveSession();
    emit websiteListChange();
    markBrowserLoadTiming(
        QStringLiteral("handleNavigateRequested total +%1ms")
            .arg(QDateTime::currentMSecsSinceEpoch() - startMs));
}

void MainController::handleBrowserAction(const QString &action)
{
    if (!m_sessionManager) {
        return;
    }

    const bool onHome = !m_browserController || m_browserController->homeVisible();
    if (onHome) {
        if (action == QStringLiteral("previousSession") || action == QStringLiteral("nextSession")) {
            const int delta = action == QStringLiteral("nextSession") ? 1 : -1;
            setFocusLayer(QStringLiteral("session"));
            moveSession(delta);
            return;
        }

        if (action == QStringLiteral("previousTab") || action == QStringLiteral("nextTab")) {
            const int delta = action == QStringLiteral("nextTab") ? 1 : -1;
            setFocusLayer(QStringLiteral("tab"));
            moveTab(delta);
            return;
        }

        if (action == QStringLiteral("homeFocusToggle")) {
            toggleFocusLayer();
            return;
        }
    }

    if (!m_browserContainer) {
        return;
    }

    if (action == QStringLiteral("previousSession") || action == QStringLiteral("nextSession")) {
        const int delta = action == QStringLiteral("nextSession") ? 1 : -1;
        const QString sessionUrl = m_sessionManager->activateAdjacentSession(delta);
        if (!sessionUrl.isEmpty()) {
            loadActiveSessionTab(sessionUrl, false);
            syncSelectionToActiveSession();
        }
        return;
    }

    if (action == QStringLiteral("previousTab") || action == QStringLiteral("nextTab")) {
        if (!m_browserController || m_browserController->homeVisible()) {
            return;
        }

        const int delta = action == QStringLiteral("nextTab") ? 1 : -1;
        const QString tabUrl = m_sessionManager->activateAdjacentTab(delta);
        if (!tabUrl.isEmpty()) {
            loadActiveSessionTab(tabUrl, false);
            syncSelectionToActiveSession();
        }
    }
}

bool MainController::closeTab(const QString &tabId)
{
    if (!m_sessionManager) {
        return false;
    }

    const QString normalizedTabId = tabId.trimmed();
    if (normalizedTabId.isEmpty()) {
        return false;
    }

    const QString ownerSessionId = m_sessionManager->sessionIdForTab(normalizedTabId);
    if (ownerSessionId.isEmpty()) {
        return false;
    }

    const QString siteId = m_sessionManager->siteIdForSession(ownerSessionId);
    const bool wasActiveSession = (ownerSessionId == m_sessionManager->activeSessionId());
    const bool isSessionActiveTab = (normalizedTabId == m_sessionManager->sessionActiveTabId(ownerSessionId));
    const bool closingDisplayedTab = wasActiveSession
        && isSessionActiveTab
        && m_browserController
        && !m_browserController->homeVisible();
    const int tabCount = m_sessionManager->tabCountForSession(ownerSessionId);

    if (tabCount <= 1) {
        enqueueBrowserViewRemovalsForTabs(m_sessionManager->tabIdsForSession(ownerSessionId));

        if (wasActiveSession) {
            leaveBrowserToHomeIfNeeded();

            if (!siteId.isEmpty()) {
                if (m_sessionManager->activeSiteId() == siteId) {
                    m_sessionManager->closeActiveSession();
                } else {
                    m_sessionManager->closeSessionForSite(siteId);
                }
            } else {
                m_sessionManager->closeSession(ownerSessionId);
            }
        } else {
            m_sessionManager->closeSession(ownerSessionId);
        }

        syncSelectionToActiveSession();
        emit websiteListChange();
        return true;
    }

    QString closedTabId;
    const QString nextTabUrl = m_sessionManager->closeTab(normalizedTabId, -1, &closedTabId);
    if (closedTabId.isEmpty()) {
        return false;
    }

    enqueueBrowserViewRemovalsForTabs(QStringList{closedTabId});

    if (closingDisplayedTab && !nextTabUrl.isEmpty()) {
        loadActiveSessionTab(nextTabUrl, false);
    }

    if (wasActiveSession) {
        syncSelectionToActiveSession();
    }
    emit websiteListChange();
    return true;
}

bool MainController::closeSiteSession(const QString &siteId)
{
    if (!m_sessionManager || !m_browserContainer) {
        return false;
    }

    const QString currentSiteId = m_browserController
        ? m_sessionManager->siteIdForUrl(m_browserController->currentUrl())
        : QString();
    const QString targetSiteId = !siteId.isEmpty()
        ? siteId
        : (!currentSiteId.isEmpty() ? currentSiteId : m_sessionManager->activeSiteId());

    const QList<BrowserSession> targetSessions = m_sessionManager->sessionsForSite(targetSiteId);
    const QString targetSessionId = targetSessions.isEmpty() ? QString() : targetSessions.constFirst().sessionId;

    leaveBrowserToHomeIfNeeded();

    if (!targetSiteId.isEmpty()) {
        if (!targetSessionId.isEmpty()) {
            enqueueBrowserViewRemovalsForTabs(m_sessionManager->tabIdsForSession(targetSessionId));
        }
        if (m_sessionManager->activeSiteId() == targetSiteId) {
            m_sessionManager->closeActiveSession();
        } else {
            m_sessionManager->closeSessionForSite(targetSiteId);
        }
    }

    emit websiteListChange();

    return true;
}

void MainController::navigateQmlToHome()
{
    if (!m_qmlEngine) {
        return;
    }

    const QList<QObject *> roots = m_qmlEngine->rootObjects();
    if (roots.isEmpty()) {
        return;
    }

    QObject *root = roots.first();
    root->setProperty("currentPage", QStringLiteral("home"));

    if (QObject *navBar = root->findChild<QObject *>(QStringLiteral("navBar"))) {
        navBar->setProperty("visible", true);
    }
}

void MainController::ensureBrowserBindings()
{
    if (m_browserController && !m_browserNavigateConnected) {
        connect(m_browserController,
                &BrowserController::navigateRequested,
                this,
                &MainController::handleNavigateRequested);
        m_browserNavigateConnected = true;
    }

    if (m_browserController && !m_browserHomeVisibleConnected) {
        connect(m_browserController, &BrowserController::homeVisibleChanged, this, [this]() {
            updatePlaybackInhibit();
        });
        m_browserHomeVisibleConnected = true;
    }

    if (!m_browserActionConnected) {
        connect(&m_inputManager, &InputManager::actionTriggered, this, &MainController::handleBrowserAction);
        m_browserActionConnected = true;
    }
}

QString MainController::openSite(const QString &url)
{
    if (!m_sessionManager) {
        return {};
    }

    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    markBrowserLoadTiming(QStringLiteral("openSite enter"));

    const qint64 sessionStartMs = QDateTime::currentMSecsSinceEpoch();
    const QString resumeUrl = m_sessionManager->activateOrCreateSiteSession(url);
    markBrowserLoadTiming(
        QStringLiteral("activateOrCreateSiteSession +%1ms resumeUrl=%2")
            .arg(QDateTime::currentMSecsSinceEpoch() - sessionStartMs)
            .arg(resumeUrl));

    if (!resumeUrl.isEmpty()) {
        const qint64 iconStartMs = QDateTime::currentMSecsSinceEpoch();
        maybeFetchIconForTemporarySession(m_sessionManager->activeSessionId());
        markBrowserLoadTiming(
            QStringLiteral("maybeFetchIcon +%1ms")
                .arg(QDateTime::currentMSecsSinceEpoch() - iconStartMs));

        loadActiveSessionTab(resumeUrl, false);

        const qint64 syncStartMs = QDateTime::currentMSecsSinceEpoch();
        syncSelectionToActiveSession();
        emit websiteListChange();
        markBrowserLoadTiming(
            QStringLiteral("sync+websiteListChange +%1ms openSite total +%2ms")
                .arg(QDateTime::currentMSecsSinceEpoch() - syncStartMs)
                .arg(QDateTime::currentMSecsSinceEpoch() - startMs));
    }
    return resumeUrl;
}

QString MainController::openHistoryUrl(const QString &url)
{
    if (!m_sessionManager) {
        return {};
    }

    const qint64 startMs = QDateTime::currentMSecsSinceEpoch();
    markBrowserLoadTiming(QStringLiteral("openHistoryUrl enter"));

    const QString sessionId = m_sessionManager->ensureSiteSession(url);
    if (sessionId.isEmpty() || m_sessionManager->activateSession(sessionId).isEmpty()) {
        return {};
    }

    const QString tabUrl = m_sessionManager->openUrlInNewTab(url, url);
    if (tabUrl.isEmpty()) {
        return {};
    }

    maybeFetchIconForTemporarySession(m_sessionManager->activeSessionId());
    loadActiveSessionTab(tabUrl, true);
    syncSelectionToActiveSession();
    emit websiteListChange();
    markBrowserLoadTiming(
        QStringLiteral("openHistoryUrl total +%1ms")
            .arg(QDateTime::currentMSecsSinceEpoch() - startMs));
    return tabUrl;
}

QString MainController::openSession(const QString &sessionId)
{
    if (!m_sessionManager) {
        return {};
    }

    const QString sessionUrl = m_sessionManager->activateSession(sessionId);
    if (sessionUrl.isEmpty()) {
        return {};
    }

    loadActiveSessionTab(sessionUrl, false);
    syncSelectionToActiveSession();
    emit websiteListChange();
    return sessionUrl;
}

QString MainController::openTab(const QString &tabId)
{
    if (!m_sessionManager) {
        return {};
    }

    const QString tabUrl = m_sessionManager->activateTab(tabId);
    if (tabUrl.isEmpty()) {
        return {};
    }

    loadActiveSessionTab(tabUrl, false);
    syncSelectionToActiveSession();
    emit websiteListChange();
    return tabUrl;
}

QString MainController::confirmSelection()
{
    if (!m_sessionManager) {
        return {};
    }

    if (m_focusLayer == QStringLiteral("tab")) {
        const QString sessionId = selectedSessionId();
        if (sessionId.isEmpty()) {
            return {};
        }

        const QList<BrowserTab> tabs = m_sessionManager->tabsForSession(sessionId);
        if (m_tabIndex < 0 || m_tabIndex >= tabs.size()) {
            return {};
        }

        return openTab(tabs.at(m_tabIndex).tabId);
    }

    const QString sessionId = selectedSessionId();
    if (sessionId.isEmpty()) {
        return {};
    }

    return openSession(sessionId);
}

int MainController::sessionIndex() const
{
    return m_sessionIndex;
}

int MainController::tabIndex() const
{
    return m_tabIndex;
}

QString MainController::focusLayer() const
{
    return m_focusLayer;
}

bool MainController::moveSession(int delta)
{
    const QList<BrowserSession> sessions = orderedOpenSessions();
    if (sessions.size() < 2 || delta == 0) {
        return false;
    }

    m_sessionIndex = (m_sessionIndex + delta + sessions.size()) % sessions.size();
    clampTabIndex();
    emit selectionChanged();
    return true;
}

bool MainController::moveTab(int delta)
{
    const QString sessionId = selectedSessionId();
    if (!m_sessionManager || sessionId.isEmpty() || delta == 0) {
        return false;
    }

    const QList<BrowserTab> tabs = m_sessionManager->tabsForSession(sessionId);
    if (tabs.size() < 2) {
        return false;
    }

    m_tabIndex = (m_tabIndex + delta + tabs.size()) % tabs.size();
    emit selectionChanged();
    return true;
}

bool MainController::setFocusLayer(const QString &layer)
{
    if (layer != QStringLiteral("session") && layer != QStringLiteral("tab")) {
        return false;
    }

    if (m_focusLayer == layer) {
        return true;
    }

    m_focusLayer = layer;
    emit selectionChanged();
    return true;
}

bool MainController::toggleFocusLayer()
{
    return setFocusLayer(m_focusLayer == QStringLiteral("session")
                             ? QStringLiteral("tab")
                             : QStringLiteral("session"));
}

QString MainController::getSessionListJson() const
{
    return buildSessionListJson();
}

QString MainController::getTabListJson(const QString &sessionId) const
{
    return buildTabListJson(sessionId);
}

int MainController::activeTabIndex() const
{
    return m_sessionManager ? m_sessionManager->activeTabIndex() : -1;
}

int MainController::activeTabCount() const
{
    return m_sessionManager ? m_sessionManager->activeTabCount() : 0;
}

QList<BrowserSession> MainController::orderedOpenSessions() const
{
    return m_sessionManager ? m_sessionManager->orderedSessions() : QList<BrowserSession>{};
}

void MainController::clampSessionIndex()
{
    const QList<BrowserSession> sessions = orderedOpenSessions();
    if (sessions.isEmpty()) {
        m_sessionIndex = 0;
        return;
    }

    m_sessionIndex = qBound(0, m_sessionIndex, sessions.size() - 1);
}

void MainController::clampTabIndex()
{
    const QString sessionId = selectedSessionId();
    if (!m_sessionManager || sessionId.isEmpty()) {
        m_tabIndex = 0;
        return;
    }

    const QList<BrowserTab> tabs = m_sessionManager->tabsForSession(sessionId);
    if (tabs.isEmpty()) {
        m_tabIndex = 0;
        return;
    }

    m_tabIndex = qBound(0, m_tabIndex, tabs.size() - 1);
}

QString MainController::selectedSessionId() const
{
    const QList<BrowserSession> sessions = orderedOpenSessions();
    if (sessions.isEmpty() || m_sessionIndex < 0 || m_sessionIndex >= sessions.size()) {
        return {};
    }

    return sessions.at(m_sessionIndex).sessionId;
}

QString MainController::buildSessionListJson() const
{
    if (!m_sessionManager) {
        return QStringLiteral("[]");
    }

    const QList<BrowserSession> sessions = orderedOpenSessions();
    const QString activeSessionId = m_sessionManager->activeSessionId();
    QJsonArray array;

    for (const BrowserSession &session : sessions) {
        QJsonObject obj;
        obj.insert(QStringLiteral("sessionId"), session.sessionId);
        obj.insert(QStringLiteral("siteName"),
                   session.siteName.trimmed().isEmpty()
                       ? m_sessionManager->siteNameForUrl(session.siteId)
                       : session.siteName);
        obj.insert(QStringLiteral("icon"), session.icon);
        obj.insert(QStringLiteral("tabCount"), tabCountForSession(session));
        obj.insert(QStringLiteral("isActive"), session.sessionId == activeSessionId);
        obj.insert(QStringLiteral("isTemporary"), sessionIsTemporary(session));
        obj.insert(QStringLiteral("updatedAt"), session.updatedAt.toUTC().toString(Qt::ISODate));
        array.append(obj);
    }

    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QString MainController::buildTabListJson(const QString &sessionId) const
{
    if (!m_sessionManager || sessionId.isEmpty()) {
        return QStringLiteral("[]");
    }

    const QList<BrowserTab> tabs = m_sessionManager->tabsForSession(sessionId);
    const QString activeTabId = m_sessionManager->sessionActiveTabId(sessionId);
    QJsonArray array;
    for (const BrowserTab &tab : tabs) {
        const bool active = !activeTabId.isEmpty()
            ? tab.tabId == activeTabId
            : (tabs.size() == 1);
        const QUrl parsedUrl(tab.currentUrl);

        QJsonObject obj;
        obj.insert(QStringLiteral("tabId"), tab.tabId);
        obj.insert(QStringLiteral("title"), tab.title);
        obj.insert(QStringLiteral("url"), tab.currentUrl);
        obj.insert(QStringLiteral("host"), parsedUrl.host());
        obj.insert(QStringLiteral("thumbnailUrl"), thumbnailUrlForTab(tab));
        obj.insert(QStringLiteral("updatedAt"), tab.updatedAt.toUTC().toString(Qt::ISODate));
        obj.insert(QStringLiteral("isActive"), active);
        array.append(obj);
    }

    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}

QString MainController::thumbnailUrlForTab(const BrowserTab &tab) const
{
    if (!tab.screenshot.isEmpty()) {
        return tab.screenshot;
    }

    if (!DatabaseManager::isInitialized()) {
        return {};
    }

    const std::optional<BrowsingHistoryItem> item = m_historyRepository.itemForUrl(normalizeHistoryUrl(tab.currentUrl));
    return item ? item->thumbnail : QString{};
}

bool MainController::sessionIsTemporary(const BrowserSession &session) const
{
    const QString normalizedSiteId = normalizedWebsiteUrl(session.siteId);
    for (const Website &website : m_repository.listAll()) {
        if (normalizedWebsiteUrl(website.url) == normalizedSiteId) {
            return false;
        }
    }

    return true;
}

int MainController::tabCountForSession(const BrowserSession &session) const
{
    return m_sessionManager ? m_sessionManager->tabCountForSession(session.sessionId) : 0;
}

void MainController::syncSelectionToActiveSession()
{
    if (!m_sessionManager) {
        return;
    }

    const QString activeSessionId = m_sessionManager->activeSessionId();
    if (activeSessionId.isEmpty()) {
        clampSessionIndex();
        clampTabIndex();
        emit selectionChanged();
        return;
    }

    const QList<BrowserSession> sessions = orderedOpenSessions();
    for (int index = 0; index < sessions.size(); ++index) {
        if (sessions.at(index).sessionId == activeSessionId) {
            if (m_sessionIndex != index) {
                m_sessionIndex = index;
            }
            break;
        }
    }

    const int activeIndex = m_sessionManager->activeTabIndex();
    if (activeIndex >= 0) {
        m_tabIndex = activeIndex;
    } else {
        clampTabIndex();
    }

    emit selectionChanged();
}
