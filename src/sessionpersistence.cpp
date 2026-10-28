#include "sessionpersistence.h"

#include <QSettings>

namespace {

constexpr char kSessionsGroup[] = "sessions";
constexpr char kActiveSessionIdKey[] = "sessions/activeSessionId";
constexpr char kItemsKey[] = "sessions/items";
constexpr char kTabsKey[] = "tabs";

QString toIsoString(const QDateTime &dateTime)
{
    return dateTime.isValid() ? dateTime.toUTC().toString(Qt::ISODate) : QString();
}

QDateTime fromIsoString(const QString &text)
{
    const QDateTime parsed = QDateTime::fromString(text, Qt::ISODate);
    return parsed.isValid() ? parsed.toUTC() : QDateTime();
}

} // namespace

void SessionPersistence::save(const QList<BrowserSession> &sessions, const QString &activeSessionId)
{
    QSettings settings;
    settings.remove(QLatin1String(kSessionsGroup));
    settings.setValue(QLatin1String(kActiveSessionIdKey), activeSessionId);

    settings.beginWriteArray(QLatin1String(kItemsKey));
    for (int index = 0; index < sessions.size(); ++index) {
        const BrowserSession &session = sessions.at(index);
        settings.setArrayIndex(index);
        settings.setValue(QStringLiteral("siteId"), session.siteId);
        settings.setValue(QStringLiteral("siteName"), session.siteName);
        settings.setValue(QStringLiteral("title"), session.title);
        settings.setValue(QStringLiteral("type"), session.type);
        settings.setValue(QStringLiteral("currentUrl"), session.currentUrl);
        settings.setValue(QStringLiteral("icon"), session.icon);
        settings.setValue(QStringLiteral("activeTabId"), session.activeTabId);
        settings.setValue(QStringLiteral("updatedAt"), toIsoString(session.updatedAt));

        settings.beginWriteArray(QLatin1String(kTabsKey));
        for (int tabIndex = 0; tabIndex < session.tabs.size(); ++tabIndex) {
            const BrowserTab &tab = session.tabs.at(tabIndex);
            settings.setArrayIndex(tabIndex);
            settings.setValue(QStringLiteral("tabId"), tab.tabId);
            settings.setValue(QStringLiteral("title"), tab.title);
            settings.setValue(QStringLiteral("currentUrl"), tab.currentUrl);
            settings.setValue(QStringLiteral("updatedAt"), toIsoString(tab.updatedAt));
        }
        settings.endArray();
    }
    settings.endArray();
    settings.sync();
}

QList<BrowserSession> SessionPersistence::load(QString *activeSessionId)
{
    QSettings settings;
    if (activeSessionId) {
        *activeSessionId = settings.value(QLatin1String(kActiveSessionIdKey)).toString();
    }

    QList<BrowserSession> sessions;
    const int sessionCount = settings.beginReadArray(QLatin1String(kItemsKey));
    for (int index = 0; index < sessionCount; ++index) {
        settings.setArrayIndex(index);

        BrowserSession session;
        session.siteId = settings.value(QStringLiteral("siteId")).toString();
        session.siteName = settings.value(QStringLiteral("siteName")).toString();
        session.title = settings.value(QStringLiteral("title")).toString();
        session.type = settings.value(QStringLiteral("type"), QStringLiteral("main")).toString();
        session.currentUrl = settings.value(QStringLiteral("currentUrl")).toString();
        session.icon = settings.value(QStringLiteral("icon")).toString();
        session.activeTabId = settings.value(QStringLiteral("activeTabId")).toString();
        session.updatedAt = fromIsoString(settings.value(QStringLiteral("updatedAt")).toString());

        const int tabCount = settings.beginReadArray(QLatin1String(kTabsKey));
        for (int tabIndex = 0; tabIndex < tabCount; ++tabIndex) {
            settings.setArrayIndex(tabIndex);
            BrowserTab tab;
            tab.tabId = settings.value(QStringLiteral("tabId")).toString();
            tab.title = settings.value(QStringLiteral("title")).toString();
            tab.currentUrl = settings.value(QStringLiteral("currentUrl")).toString();
            tab.updatedAt = fromIsoString(settings.value(QStringLiteral("updatedAt")).toString());
            session.tabs.append(tab);
        }
        settings.endArray();

        sessions.append(session);
    }
    settings.endArray();

    return sessions;
}
