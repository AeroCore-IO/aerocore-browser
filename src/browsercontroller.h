#pragma once

#include <QObject>
#include <QUrl>

class BrowserController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString currentUrl READ currentUrl NOTIFY pageStateChanged)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY pageStateChanged)
    Q_PROPERTY(bool homeVisible READ homeVisible NOTIFY homeVisibleChanged)
    Q_PROPERTY(bool hudVisible READ hudVisible WRITE setHudVisible NOTIFY hudVisibleChanged)
    Q_PROPERTY(bool canGoBack READ canGoBack NOTIFY navigationStateChanged)
    Q_PROPERTY(bool canGoForward READ canGoForward NOTIFY navigationStateChanged)

public:
    explicit BrowserController(QObject *parent = nullptr);

    QString currentUrl() const;
    QString currentTitle() const;
    bool homeVisible() const;
    bool hudVisible() const;
    bool canGoBack() const;
    bool canGoForward() const;

    Q_INVOKABLE void openUrl(const QString &input);
    Q_INVOKABLE void goHome();
    Q_INVOKABLE void showBrowser();
    Q_INVOKABLE void setPageState(const QString &url, const QString &title, bool canGoBack, bool canGoForward);
    Q_INVOKABLE void setHudVisible(bool visible);
    Q_INVOKABLE void toggleHud();

signals:
    void navigateRequested(const QString &url);
    void pageStateChanged();
    void homeVisibleChanged();
    void hudVisibleChanged();
    void navigationStateChanged();

private:
    static QUrl normalizeInput(const QString &input);
    void setHomeVisible(bool visible);

    QString m_currentUrl;
    QString m_currentTitle;
    bool m_homeVisible = true;
    bool m_hudVisible = false;
    bool m_canGoBack = false;
    bool m_canGoForward = false;
};