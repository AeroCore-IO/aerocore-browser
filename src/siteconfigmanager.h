#pragma once

#include <QHash>
#include <QObject>

struct SiteConfig
{
    double zoomFactor = 1.2;
    int minimumFontSize = 20;
    bool desktopMode = true;
    QString readabilityMode = QStringLiteral("sofa");
};

struct SiteConfigSnapshot
{
    double appUiScale = 1.0;
    bool hasExplicitAppUiScale = false;
    double defaultZoomFactor = 1.2;
    bool hasExplicitDefaultZoomFactor = false;
    int defaultMinimumFontSize = 20;
    bool hasExplicitDefaultMinimumFontSize = false;
    QHash<QString, SiteConfig> siteConfigs;
};

class SiteConfigManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double appUiScale READ appUiScale WRITE setAppUiScale NOTIFY appUiScaleChanged)
    Q_PROPERTY(double defaultZoomFactor READ defaultZoomFactor WRITE setDefaultZoomFactor NOTIFY defaultsChanged)
    Q_PROPERTY(int defaultMinimumFontSize READ defaultMinimumFontSize WRITE setDefaultMinimumFontSize NOTIFY defaultsChanged)
    Q_PROPERTY(QString desktopUserAgent READ desktopUserAgent CONSTANT)

public:
    explicit SiteConfigManager(QObject *parent = nullptr);

    double appUiScale() const;
    double defaultZoomFactor() const;
    int defaultMinimumFontSize() const;
    QString desktopUserAgent() const;
    QString automaticReadabilitySummary() const;

    void setAppUiScale(double factor);
    void setDefaultZoomFactor(double factor);
    void setDefaultMinimumFontSize(int size);
    Q_INVOKABLE void resetAppUiScale();
    Q_INVOKABLE void resetDefaultZoomFactor();
    Q_INVOKABLE void resetDefaultMinimumFontSize();

    bool hasExplicitAppUiScale() const { return m_hasExplicitAppUiScale; }
    bool hasExplicitDefaultZoomFactor() const { return m_hasExplicitDefaultZoomFactor; }
    bool hasExplicitDefaultMinimumFontSize() const { return m_hasExplicitDefaultMinimumFontSize; }

    SiteConfigSnapshot exportSnapshot() const;
    void applySnapshot(const SiteConfigSnapshot &snapshot);

    Q_INVOKABLE double zoomForUrl(const QString &url) const;
    Q_INVOKABLE int minimumFontForUrl(const QString &url) const;
    Q_INVOKABLE bool desktopModeForUrl(const QString &url) const;
    Q_INVOKABLE QString readabilityModeForUrl(const QString &url) const;
    Q_INVOKABLE void setZoomForUrl(const QString &url, double factor);
    Q_INVOKABLE void setMinimumFontSizeForUrl(const QString &url, int size);
    Q_INVOKABLE void setReadabilityModeForUrl(const QString &url, const QString &mode);
    Q_INVOKABLE void resetSite(const QString &url);
    Q_INVOKABLE void refreshAutomaticReadability();

    // Synthetic-screen probe for tests / diagnostics (logical geometry + DPR + diagonal).
    static double probeAutomaticPageZoom(int logicalWidth,
                                         int logicalHeight,
                                         qreal devicePixelRatio,
                                         double diagonalInches);
    static QString probeAutomaticTierLabel(int logicalWidth,
                                           int logicalHeight,
                                           qreal devicePixelRatio,
                                           double diagonalInches);

signals:
    void appUiScaleChanged(double factor);
    void defaultsChanged();
    void siteConfigChanged(const QString &host);

public:
    void save() const;

private:
    SiteConfig configForHost(const QString &host) const;
    double automaticSystemUiScale() const;
    double automaticAppUiScale() const;
    double automaticDefaultZoomFactor() const;
    int automaticDefaultMinimumFontSize() const;
    static QString hostFromUrl(const QString &url);

    double m_appUiScale = 1.0;
    bool m_hasExplicitAppUiScale = false;
    double m_defaultZoomFactor = 1.2;
    bool m_hasExplicitDefaultZoomFactor = false;
    int m_defaultMinimumFontSize = 20;
    bool m_hasExplicitDefaultMinimumFontSize = false;
    QHash<QString, SiteConfig> m_siteConfigs;
};