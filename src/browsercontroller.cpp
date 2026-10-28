#include "browsercontroller.h"

#include <QUrlQuery>

BrowserController::BrowserController(QObject *parent)
    : QObject(parent)
{
}

QString BrowserController::currentUrl() const
{
    return m_currentUrl;
}

QString BrowserController::currentTitle() const
{
    return m_currentTitle;
}

bool BrowserController::homeVisible() const
{
    return m_homeVisible;
}

bool BrowserController::hudVisible() const
{
    return m_hudVisible;
}

bool BrowserController::canGoBack() const
{
    return m_canGoBack;
}

bool BrowserController::canGoForward() const
{
    return m_canGoForward;
}

void BrowserController::openUrl(const QString &input)
{
    const QUrl target = normalizeInput(input);
    if (!target.isValid() || target.isEmpty()) {
        return;
    }

    setHomeVisible(false);
    emit navigateRequested(target.toString());
}

void BrowserController::goHome()
{
    setHomeVisible(true);
}

void BrowserController::showBrowser()
{
    setHomeVisible(false);
}

void BrowserController::setPageState(const QString &url, const QString &title, bool canGoBack, bool canGoForward)
{
    bool pageChanged = false;
    bool navChanged = false;

    if (m_currentUrl != url) {
        m_currentUrl = url;
        pageChanged = true;
    }

    if (m_currentTitle != title) {
        m_currentTitle = title;
        pageChanged = true;
    }

    if (m_canGoBack != canGoBack) {
        m_canGoBack = canGoBack;
        navChanged = true;
    }

    if (m_canGoForward != canGoForward) {
        m_canGoForward = canGoForward;
        navChanged = true;
    }

    if (pageChanged) {
        emit pageStateChanged();
    }

    if (navChanged) {
        emit navigationStateChanged();
    }
}

void BrowserController::setHudVisible(bool visible)
{
    if (m_hudVisible == visible) {
        return;
    }

    m_hudVisible = visible;
    emit hudVisibleChanged();
}

void BrowserController::toggleHud()
{
    setHudVisible(!m_hudVisible);
}

QUrl BrowserController::normalizeInput(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    if (trimmed.startsWith(QStringLiteral("about:"), Qt::CaseInsensitive)) {
        return QUrl(trimmed);
    }

    if (!trimmed.contains(QLatin1Char(' ')) && (trimmed.contains(QLatin1Char('.')) || trimmed.startsWith(QStringLiteral("localhost")))) {
        return QUrl::fromUserInput(trimmed);
    }

    QUrl searchUrl(QStringLiteral("https://www.bing.com/search"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), trimmed);
    searchUrl.setQuery(query);
    return searchUrl;
}

void BrowserController::setHomeVisible(bool visible)
{
    if (m_homeVisible == visible) {
        return;
    }

    m_homeVisible = visible;
    emit homeVisibleChanged();
}