#include "siteconfigmanager.h"

#include "setting/settingsstore.h"

#include <algorithm>
#include <cmath>
#include <QDebug>
#include <QGuiApplication>
#include <QSettings>
#include <QScreen>
#include <QStringList>
#include <QUrl>

namespace {
enum class ViewingProfileKind {
    Handheld,
    Tv,
};

enum class ReadabilityTier {
    Handheld,
    Tv720,
    Tv1080,
    Tv1440,
    Tv4k,
};

struct TierReadability {
    double appUiScale;
    double pageZoom;
    int minimumFontSize;
};

struct ViewingProfile {
    ViewingProfileKind kind = ViewingProfileKind::Tv;
    ReadabilityTier tier = ReadabilityTier::Tv1080;
    QString reason = QStringLiteral("living-room default");
    QRect geometry;
    QSizeF physicalSizeMm;
    double diagonalInches = 0.0;
    double dpi = 96.0;
    qreal compositorScale = 1.0;
};

bool hasUsablePhysicalSize(const QScreen *screen)
{
    if (!screen) {
        return false;
    }

    const QSizeF physicalSize = screen->physicalSize();
    return physicalSize.width() > 100.0 && physicalSize.height() > 100.0;
}

double screenDiagonalInches(const QScreen *screen)
{
    if (!hasUsablePhysicalSize(screen)) {
        return 0.0;
    }

    const QSizeF physicalSize = screen->physicalSize();
    return std::hypot(physicalSize.width(), physicalSize.height()) / 25.4;
}

QRect resolutionGeometryForTier(const QRect &logicalGeometry, qreal compositorScale)
{
    const qreal dpr = std::max<qreal>(compositorScale, 1.0);
    if (dpr <= 1.01 || logicalGeometry.isEmpty()) {
        return logicalGeometry;
    }

    // Qt often reports logical geometry under OS/compositor scaling (e.g. 1920x1080
    // on a 4K panel at DPR=2). Tier selection must use physical pixels so 4K still
    // lands in TV-4K rather than TV-1080p.
    return QRect(0,
                 0,
                 qRound(logicalGeometry.width() * dpr),
                 qRound(logicalGeometry.height() * dpr));
}

double effectiveScreenDpi(const QScreen *screen)
{
    if (!screen) {
        return 96.0;
    }

    const QRect geometry = screen->geometry();
    const qreal dpr = std::max<qreal>(screen->devicePixelRatio(), 1.0);
    if (hasUsablePhysicalSize(screen) && !geometry.isEmpty()) {
        const QSizeF physicalSize = screen->physicalSize();
        const double dpiX = (geometry.width() * dpr) / (physicalSize.width() / 25.4);
        const double dpiY = (geometry.height() * dpr) / (physicalSize.height() / 25.4);
        const double computedDpi = (dpiX + dpiY) * 0.5;
        if (std::isfinite(computedDpi) && computedDpi >= 20.0 && computedDpi <= 400.0) {
            return computedDpi;
        }
    }

    double dpi = screen->physicalDotsPerInch();
    if (!std::isfinite(dpi) || dpi < 20.0 || dpi > 400.0) {
        dpi = screen->logicalDotsPerInch();
    }
    if (!std::isfinite(dpi) || dpi < 20.0 || dpi > 400.0) {
        dpi = 96.0;
    }
    return dpi;
}

QString normalizedViewingModeOverride()
{
    const QString value = qEnvironmentVariable("AEROCORE_VIEWING_MODE").trimmed().toLower();
    if (value == QStringLiteral("tv")
        || value == QStringLiteral("couch")
        || value == QStringLiteral("10foot")
        || value == QStringLiteral("tenfoot")) {
        return QStringLiteral("tv");
    }

    if (value == QStringLiteral("handheld")
        || value == QStringLiteral("portable")
        || value == QStringLiteral("nearfield")) {
        return QStringLiteral("handheld");
    }

    if (value == QStringLiteral("desktop")
        || value == QStringLiteral("monitor")
        || value == QStringLiteral("pc")) {
        return QStringLiteral("handheld");
    }

    return {};
}

bool containsAnyHint(const QString &value, std::initializer_list<const char *> hints)
{
    const QString normalized = value.trimmed().toLower();
    if (normalized.isEmpty()) {
        return false;
    }

    for (const char *hint : hints) {
        if (normalized.contains(QString::fromLatin1(hint))) {
            return true;
        }
    }

    return false;
}

bool hasTvSessionHint()
{
    const QStringList desktopValues{
        qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
        qEnvironmentVariable("XDG_SESSION_DESKTOP"),
        qEnvironmentVariable("DESKTOP_SESSION")
    };

    for (const QString &value : desktopValues) {
        if (containsAnyHint(value, {"gamescope", "steamos", "plasma-bigscreen", "kodi", "xbmc"})) {
            return true;
        }
    }

    return qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY")
        || qEnvironmentVariableIsSet("GAMESCOPE_SESSION");
}

bool hasHandheldSessionHint()
{
    const QStringList sessionValues{
        qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
        qEnvironmentVariable("XDG_SESSION_DESKTOP"),
        qEnvironmentVariable("DESKTOP_SESSION"),
        qEnvironmentVariable("XDG_SESSION_TYPE"),
        qEnvironmentVariable("SteamDeck")
    };

    for (const QString &value : sessionValues) {
        if (containsAnyHint(value, {"steamdeck", "gamescope", "steamos", "handheld"})) {
            return true;
        }
    }

    return qEnvironmentVariableIsSet("SteamDeck")
        || qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY")
        || qEnvironmentVariableIsSet("GAMESCOPE_SESSION");
}

bool hasTvAspectRatio(const QRect &geometry)
{
    if (geometry.width() <= 0 || geometry.height() <= 0) {
        return false;
    }

    const double aspect = double(std::max(geometry.width(), geometry.height()))
        / double(std::min(geometry.width(), geometry.height()));
    return aspect >= 1.60 && aspect <= 1.90;
}

bool hasUltraHdTvResolution(const QRect &geometry)
{
    return geometry.width() >= 3200 && geometry.height() >= 1800 && hasTvAspectRatio(geometry);
}

ReadabilityTier readabilityTierForGeometry(const QRect &geometry, ViewingProfileKind kind)
{
    if (kind == ViewingProfileKind::Handheld) {
        return ReadabilityTier::Handheld;
    }

    const int height = std::max(geometry.width(), geometry.height()) >= std::min(geometry.width(), geometry.height())
        ? std::min(geometry.width(), geometry.height())
        : geometry.height();

    if (height >= 1800 || geometry.width() >= 3200) {
        return ReadabilityTier::Tv4k;
    }
    if (height >= 1400 || geometry.width() >= 2500) {
        return ReadabilityTier::Tv1440;
    }
    if (height >= 1000 || geometry.width() >= 1700) {
        return ReadabilityTier::Tv1080;
    }
    return ReadabilityTier::Tv720;
}

QString readabilityTierLabel(ReadabilityTier tier)
{
    switch (tier) {
    case ReadabilityTier::Handheld:
        return QStringLiteral("Handheld");
    case ReadabilityTier::Tv720:
        return QStringLiteral("TV-720p");
    case ReadabilityTier::Tv1080:
        return QStringLiteral("TV-1080p");
    case ReadabilityTier::Tv1440:
        return QStringLiteral("TV-1440p");
    case ReadabilityTier::Tv4k:
        return QStringLiteral("TV-4K");
    }

    return QStringLiteral("TV-1080p");
}

bool looksLikeHandheld(const ViewingProfile &profile)
{
    const bool smallPhysicalHint = profile.diagonalInches > 0.0 && profile.diagonalInches <= 10.5;
    const bool veryHighDpiHint = profile.dpi >= 165.0;
    const bool compactResolutionHint = profile.geometry.width() > 0
        && profile.geometry.height() > 0
        && std::max(profile.geometry.width(), profile.geometry.height()) <= 1920
        && std::min(profile.geometry.width(), profile.geometry.height()) <= 1200;
    const bool ultraCompactResolutionHint = profile.geometry.width() > 0
        && profile.geometry.height() > 0
        && std::max(profile.geometry.width(), profile.geometry.height()) <= 1600
        && std::min(profile.geometry.width(), profile.geometry.height()) <= 900;
    const bool handheldSessionHint = hasHandheldSessionHint();

    if (smallPhysicalHint && compactResolutionHint) {
        return true;
    }
    if (veryHighDpiHint && ultraCompactResolutionHint) {
        return true;
    }
    // Session hints (gamescope / steamos / bazzite) alone must not classify a
    // living-room 4K TV as handheld when Qt reports a scaled 1080p logical size
    // with DPR>=1.25 — that previously forced ~1.10 page zoom.
    if (handheldSessionHint && compactResolutionHint && (smallPhysicalHint || veryHighDpiHint)) {
        return true;
    }

    return false;
}

ViewingProfile detectViewingProfile(const QScreen *screen)
{
    ViewingProfile profile;
    if (!screen) {
        profile.reason = QStringLiteral("no screen");
        return profile;
    }

    profile.geometry = screen->geometry();
    profile.physicalSizeMm = screen->physicalSize();
    profile.diagonalInches = screenDiagonalInches(screen);
    profile.dpi = effectiveScreenDpi(screen);
    profile.compositorScale = screen->devicePixelRatio();

    const QString overrideMode = normalizedViewingModeOverride();
    if (overrideMode == QStringLiteral("tv")) {
        profile.kind = ViewingProfileKind::Tv;
        profile.reason = QStringLiteral("AEROCORE_VIEWING_MODE=tv");
        profile.tier = readabilityTierForGeometry(
            resolutionGeometryForTier(profile.geometry, profile.compositorScale),
            profile.kind);
        return profile;
    }
    if (overrideMode == QStringLiteral("handheld")) {
        profile.kind = ViewingProfileKind::Handheld;
        profile.reason = QStringLiteral("AEROCORE_VIEWING_MODE=handheld");
        profile.tier = readabilityTierForGeometry(
            resolutionGeometryForTier(profile.geometry, profile.compositorScale),
            profile.kind);
        return profile;
    }

    const bool handheld = looksLikeHandheld(profile);
    const bool tvSessionHint = hasTvSessionHint();

    profile.kind = handheld ? ViewingProfileKind::Handheld : ViewingProfileKind::Tv;
    profile.tier = readabilityTierForGeometry(
        resolutionGeometryForTier(profile.geometry, profile.compositorScale),
        profile.kind);
    if (handheld) {
        profile.reason = QStringLiteral("compact high-density display");
    } else if (tvSessionHint) {
        if (profile.diagonalInches > 0.0 && profile.diagonalInches < 32.0) {
            profile.reason = QStringLiteral("living-room session default; ignored undersized EDID");
        } else {
            profile.reason = QStringLiteral("living-room session default");
        }
    } else {
        profile.reason = QStringLiteral("non-handheld default");
    }
    return profile;
}

QString viewingProfileKindLabel(ViewingProfileKind kind)
{
    return kind == ViewingProfileKind::Handheld ? QStringLiteral("Handheld") : QStringLiteral("Living-room");
}

double automaticDesktopSystemUiScale(const QScreen *screen)
{
    if (!screen) {
        return 1.0;
    }

    const qreal compositorScale = screen->devicePixelRatio();
    if (compositorScale > 1.01) {
        return compositorScale;
    }

    const qreal logicalDpiScale = screen->logicalDotsPerInch() > 1.0
        ? screen->logicalDotsPerInch() / 96.0
        : 1.0;
    if (logicalDpiScale > 1.01) {
        return logicalDpiScale;
    }

    const QRect geometry = screen->geometry();
    const qreal widthScale = geometry.width() > 0 ? geometry.width() / 1920.0 : 1.0;
    const qreal heightScale = geometry.height() > 0 ? geometry.height() / 1080.0 : 1.0;
    const qreal resolutionScale = std::pow(std::max(widthScale, heightScale), 0.5);

    qreal referenceDpi = screen->logicalDotsPerInch();
    const qreal physicalDpi = screen->physicalDotsPerInch();
    if (physicalDpi > 1.0) {
        referenceDpi = std::min(referenceDpi, physicalDpi);
    }
    if (referenceDpi <= 1.0) {
        referenceDpi = 96.0;
    }

    const qreal densityScale = 96.0 / referenceDpi;
    return std::max({1.0, resolutionScale, densityScale});
}

double progressBetween(double value, double start, double end)
{
    if (!std::isfinite(value)) {
        return 0.0;
    }
    if (end <= start) {
        return value >= end ? 1.0 : 0.0;
    }
    return std::clamp((value - start) / (end - start), 0.0, 1.0);
}

double lerp(double start, double end, double t)
{
    return start + ((end - start) * std::clamp(t, 0.0, 1.0));
}

TierReadability tierBaseReadability(ReadabilityTier tier)
{
    switch (tier) {
    case ReadabilityTier::Handheld:
        return {1.00, 1.10, 17};
    case ReadabilityTier::Tv720:
        return {1.95, 1.74, 22};
    case ReadabilityTier::Tv1080:
        return {2.00, 1.78, 22};
    case ReadabilityTier::Tv1440:
        return {2.08, 1.82, 22};
    case ReadabilityTier::Tv4k:
        return {2.20, 1.90, 23};
    }

    return {2.00, 1.78, 22};
}

TierReadability tierSmallScreenReadability(ReadabilityTier tier)
{
    switch (tier) {
    case ReadabilityTier::Handheld:
        return {1.00, 1.10, 17};
    case ReadabilityTier::Tv720:
        return {1.55, 1.46, 20};
    case ReadabilityTier::Tv1080:
        return {1.60, 1.50, 20};
    case ReadabilityTier::Tv1440:
        return {1.68, 1.56, 20};
    case ReadabilityTier::Tv4k:
        return {1.80, 1.63, 21};
    }

    return {1.60, 1.50, 20};
}

TierReadability tierLargeScreenReadability(ReadabilityTier tier)
{
    switch (tier) {
    case ReadabilityTier::Handheld:
        return {1.12, 1.18, 18};
    case ReadabilityTier::Tv720:
        return {2.08, 1.82, 23};
    case ReadabilityTier::Tv1080:
        return {2.14, 1.86, 23};
    case ReadabilityTier::Tv1440:
        return {2.21, 1.90, 24};
    case ReadabilityTier::Tv4k:
        return {2.35, 2.00, 26};
    }

    return {2.14, 1.86, 23};
}

TierReadability interpolateReadability(const TierReadability &from, const TierReadability &to, double t)
{
    return {
        lerp(from.appUiScale, to.appUiScale, t),
        lerp(from.pageZoom, to.pageZoom, t),
        qRound(lerp(double(from.minimumFontSize), double(to.minimumFontSize), t))
    };
}

TierReadability automaticHandheldReadability(const ViewingProfile &profile)
{
    constexpr double kHandheldAnchorDiagonal = 7.4;
    constexpr double kHandheldMaxDiagonal = 11.0;

    const TierReadability anchor = tierBaseReadability(ReadabilityTier::Handheld);
    const TierReadability larger = tierLargeScreenReadability(ReadabilityTier::Handheld);
    const double sizeProgress = profile.diagonalInches > 0.0
        ? progressBetween(profile.diagonalInches, kHandheldAnchorDiagonal, kHandheldMaxDiagonal)
        : progressBetween(double(std::max(profile.geometry.width(), profile.geometry.height())), 1280.0, 1920.0);

    return interpolateReadability(anchor, larger, sizeProgress);
}

TierReadability automaticLivingRoomReadability(const ViewingProfile &profile)
{
    constexpr double kSmallScreenDiagonal = 32.0;
    constexpr double kAnchorDiagonal = 45.88;
    constexpr double kLargeScreenDiagonal = 85.0;

    const TierReadability small = tierSmallScreenReadability(profile.tier);
    const TierReadability anchor = tierBaseReadability(profile.tier);
    const TierReadability large = tierLargeScreenReadability(profile.tier);

    // gamescope / SteamOS living-room sessions often inherit PC-monitor EDIDs
    // (e.g. 27"/162dpi on a 4K panel). That collapses zoom to the small-screen
    // floor (Tv4k → 1.63) instead of the couch anchor (1.90) that es-version
    // calibration used. Below the living-room curve floor, treat size as unknown.
    double diagonalInches = profile.diagonalInches;
    if (diagonalInches > 0.0 && diagonalInches < kSmallScreenDiagonal && hasTvSessionHint()) {
        diagonalInches = 0.0;
    }

    if (diagonalInches <= 0.0) {
        return anchor;
    }
    if (diagonalInches <= kAnchorDiagonal) {
        return interpolateReadability(small,
                                      anchor,
                                      std::pow(progressBetween(diagonalInches, kSmallScreenDiagonal, kAnchorDiagonal), 1.05));
    }
    return interpolateReadability(anchor,
                                  large,
                                  std::pow(progressBetween(diagonalInches, kAnchorDiagonal, kLargeScreenDiagonal), 1.10));
}

TierReadability automaticReadability(const ViewingProfile &profile)
{
    if (profile.kind == ViewingProfileKind::Handheld) {
        return automaticHandheldReadability(profile);
    }
    return automaticLivingRoomReadability(profile);
}

QString formatAutomaticReadabilitySummary(const ViewingProfile &profile)
{
    const QString diagonalText = profile.diagonalInches > 0.0
        ? QStringLiteral("%1\"").arg(QString::number(profile.diagonalInches, 'f', 0))
        : QStringLiteral("unknown size");
    return QStringLiteral("Auto profile: %1 / %2 (%3)  Screen: %4x%5  %6  %7 dpi")
        .arg(viewingProfileKindLabel(profile.kind),
             readabilityTierLabel(profile.tier),
             profile.reason,
             QString::number(profile.geometry.width()),
             QString::number(profile.geometry.height()),
             diagonalText,
             QString::number(profile.dpi, 'f', 0));
}

QString formatAutomaticReadabilityInputs(const QScreen *screen, const ViewingProfile &profile)
{
    const double logicalDpi = screen ? screen->logicalDotsPerInch() : 0.0;
    const double physicalDpi = screen ? screen->physicalDotsPerInch() : 0.0;
    const QRect tierGeometry =
        resolutionGeometryForTier(profile.geometry, profile.compositorScale);
    return QStringLiteral(
               "Readability inputs: class=%1 tier=%2 geometry=%3x%4 tierGeometry=%5x%6 physical=%7x%8mm dpr=%9 logicalDpi=%10 physicalDpi=%11 effectiveDpi=%12 diagonal=%13in uhd=%14")
        .arg(viewingProfileKindLabel(profile.kind),
             readabilityTierLabel(profile.tier),
             QString::number(profile.geometry.width()),
             QString::number(profile.geometry.height()),
             QString::number(tierGeometry.width()),
             QString::number(tierGeometry.height()),
             QString::number(profile.physicalSizeMm.width(), 'f', 1),
             QString::number(profile.physicalSizeMm.height(), 'f', 1),
             QString::number(profile.compositorScale, 'f', 2),
             QString::number(logicalDpi, 'f', 2),
             QString::number(physicalDpi, 'f', 2),
             QString::number(profile.dpi, 'f', 2),
             QString::number(profile.diagonalInches, 'f', 2),
             hasUltraHdTvResolution(tierGeometry) ? QStringLiteral("true") : QStringLiteral("false"));
}

QString formatAutomaticReadabilityOutputs(const ViewingProfile &profile)
{
    const TierReadability readability = automaticReadability(profile);
    return QStringLiteral("Readability outputs: appUiScale=%1 pageZoom=%2 minimumFontSize=%3")
        .arg(QString::number(readability.appUiScale, 'f', 2),
             QString::number(readability.pageZoom, 'f', 2),
             QString::number(readability.minimumFontSize));
}

double zoomFactorForReadabilityMode(const QString &mode, double baseZoom)
{
    if (mode == QStringLiteral("large")) {
        return std::clamp(baseZoom + 0.15, 0.8, 3.0);
    }
    if (mode == QStringLiteral("standard")) {
        return std::clamp(baseZoom - 0.15, 0.8, 3.0);
    }
    return std::clamp(baseZoom, 0.8, 3.0);
}

int minimumFontSizeForReadabilityMode(const QString &mode, int baseMinimumFontSize)
{
    if (mode == QStringLiteral("large")) {
        return std::clamp(baseMinimumFontSize + 3, 12, 28);
    }
    if (mode == QStringLiteral("standard")) {
        return std::clamp(baseMinimumFontSize - 2, 12, 28);
    }
    return std::clamp(baseMinimumFontSize, 12, 28);
}

}

SiteConfigManager::SiteConfigManager(QObject *parent)
    : QObject(parent)
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const ViewingProfile profile = detectViewingProfile(screen);
    qInfo().noquote() << formatAutomaticReadabilitySummary(profile);
    qInfo().noquote() << formatAutomaticReadabilityInputs(screen, profile);
    qInfo().noquote() << formatAutomaticReadabilityOutputs(profile);
}

double SiteConfigManager::appUiScale() const
{
    return m_hasExplicitAppUiScale ? m_appUiScale : automaticAppUiScale();
}

double SiteConfigManager::defaultZoomFactor() const
{
    return m_hasExplicitDefaultZoomFactor ? m_defaultZoomFactor : automaticDefaultZoomFactor();
}

int SiteConfigManager::defaultMinimumFontSize() const
{
    return m_hasExplicitDefaultMinimumFontSize ? m_defaultMinimumFontSize : automaticDefaultMinimumFontSize();
}

QString SiteConfigManager::desktopUserAgent() const
{
    return QStringLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/136.0.0.0 Safari/537.36");
}

QString SiteConfigManager::automaticReadabilitySummary() const
{
    return formatAutomaticReadabilitySummary(detectViewingProfile(QGuiApplication::primaryScreen()));
}

ViewingProfile profileFromProbeInputs(int logicalWidth,
                                      int logicalHeight,
                                      qreal devicePixelRatio,
                                      double diagonalInches)
{
    ViewingProfile profile;
    profile.geometry = QRect(0, 0, std::max(0, logicalWidth), std::max(0, logicalHeight));
    profile.compositorScale = std::max<qreal>(devicePixelRatio, 1.0);
    profile.diagonalInches = std::max(0.0, diagonalInches);

    const double physWidthPx = profile.geometry.width() * profile.compositorScale;
    const double physHeightPx = profile.geometry.height() * profile.compositorScale;
    const double diagPx = std::hypot(physWidthPx, physHeightPx);
    profile.dpi = (profile.diagonalInches > 0.0 && diagPx > 0.0)
        ? diagPx / profile.diagonalInches
        : 96.0;

    const bool handheld = looksLikeHandheld(profile);
    profile.kind = handheld ? ViewingProfileKind::Handheld : ViewingProfileKind::Tv;
    profile.tier = readabilityTierForGeometry(
        resolutionGeometryForTier(profile.geometry, profile.compositorScale),
        profile.kind);
    profile.reason = handheld ? QStringLiteral("probe-handheld") : QStringLiteral("probe-tv");
    return profile;
}

double SiteConfigManager::probeAutomaticPageZoom(int logicalWidth,
                                                 int logicalHeight,
                                                 qreal devicePixelRatio,
                                                 double diagonalInches)
{
    return automaticReadability(
               profileFromProbeInputs(logicalWidth, logicalHeight, devicePixelRatio, diagonalInches))
        .pageZoom;
}

QString SiteConfigManager::probeAutomaticTierLabel(int logicalWidth,
                                                   int logicalHeight,
                                                   qreal devicePixelRatio,
                                                   double diagonalInches)
{
    return readabilityTierLabel(
        profileFromProbeInputs(logicalWidth, logicalHeight, devicePixelRatio, diagonalInches).tier);
}

void SiteConfigManager::refreshAutomaticReadability()
{
    const QScreen *screen = QGuiApplication::primaryScreen();
    const ViewingProfile profile = detectViewingProfile(screen);
    qInfo().noquote() << formatAutomaticReadabilitySummary(profile);
    qInfo().noquote() << formatAutomaticReadabilityInputs(screen, profile);
    qInfo().noquote() << formatAutomaticReadabilityOutputs(profile);
    emit appUiScaleChanged(appUiScale());
    emit defaultsChanged();
}

void SiteConfigManager::setAppUiScale(double factor)
{
    const double bounded = std::clamp(factor, 1.0, 2.4);
    if (m_hasExplicitAppUiScale && qFuzzyCompare(m_appUiScale, bounded)) {
        return;
    }

    m_appUiScale = bounded;
    m_hasExplicitAppUiScale = true;
    save();
    emit appUiScaleChanged(appUiScale());
}

void SiteConfigManager::setDefaultZoomFactor(double factor)
{
    const double bounded = std::clamp(factor, 0.8, 3.0);
    if (m_hasExplicitDefaultZoomFactor && qFuzzyCompare(m_defaultZoomFactor, bounded)) {
        return;
    }

    m_defaultZoomFactor = bounded;
    m_hasExplicitDefaultZoomFactor = true;
    save();
    emit defaultsChanged();
}

void SiteConfigManager::setDefaultMinimumFontSize(int size)
{
    const int bounded = std::clamp(size, 12, 28);
    if (m_hasExplicitDefaultMinimumFontSize && m_defaultMinimumFontSize == bounded) {
        return;
    }

    m_defaultMinimumFontSize = bounded;
    m_hasExplicitDefaultMinimumFontSize = true;
    save();
    emit defaultsChanged();
}

void SiteConfigManager::resetAppUiScale()
{
    if (!m_hasExplicitAppUiScale) {
        return;
    }

    m_hasExplicitAppUiScale = false;
    m_appUiScale = 1.0;
    save();
    emit appUiScaleChanged(appUiScale());
}

void SiteConfigManager::resetDefaultZoomFactor()
{
    if (!m_hasExplicitDefaultZoomFactor) {
        return;
    }

    m_hasExplicitDefaultZoomFactor = false;
    m_defaultZoomFactor = 1.2;
    save();
    emit defaultsChanged();
}

void SiteConfigManager::resetDefaultMinimumFontSize()
{
    if (!m_hasExplicitDefaultMinimumFontSize) {
        return;
    }

    m_hasExplicitDefaultMinimumFontSize = false;
    m_defaultMinimumFontSize = 20;
    save();
    emit defaultsChanged();
}

SiteConfigSnapshot SiteConfigManager::exportSnapshot() const
{
    SiteConfigSnapshot snapshot;
    snapshot.appUiScale = m_appUiScale;
    snapshot.hasExplicitAppUiScale = m_hasExplicitAppUiScale;
    snapshot.defaultZoomFactor = m_defaultZoomFactor;
    snapshot.hasExplicitDefaultZoomFactor = m_hasExplicitDefaultZoomFactor;
    snapshot.defaultMinimumFontSize = m_defaultMinimumFontSize;
    snapshot.hasExplicitDefaultMinimumFontSize = m_hasExplicitDefaultMinimumFontSize;
    snapshot.siteConfigs = m_siteConfigs;
    return snapshot;
}

void SiteConfigManager::applySnapshot(const SiteConfigSnapshot &snapshot)
{
    m_appUiScale = snapshot.appUiScale;
    m_hasExplicitAppUiScale = snapshot.hasExplicitAppUiScale;
    m_defaultZoomFactor = snapshot.defaultZoomFactor;
    m_hasExplicitDefaultZoomFactor = snapshot.hasExplicitDefaultZoomFactor;
    m_defaultMinimumFontSize = snapshot.defaultMinimumFontSize;
    m_hasExplicitDefaultMinimumFontSize = snapshot.hasExplicitDefaultMinimumFontSize;
    m_siteConfigs = snapshot.siteConfigs;
}

double SiteConfigManager::zoomForUrl(const QString &url) const
{
    return configForHost(hostFromUrl(url)).zoomFactor;
}

int SiteConfigManager::minimumFontForUrl(const QString &url) const
{
    return configForHost(hostFromUrl(url)).minimumFontSize;
}

bool SiteConfigManager::desktopModeForUrl(const QString &url) const
{
    return configForHost(hostFromUrl(url)).desktopMode;
}

QString SiteConfigManager::readabilityModeForUrl(const QString &url) const
{
    return configForHost(hostFromUrl(url)).readabilityMode;
}

void SiteConfigManager::setZoomForUrl(const QString &url, double factor)
{
    const QString host = hostFromUrl(url);
    if (host.isEmpty()) {
        setDefaultZoomFactor(factor);
        return;
    }

    SiteConfig config = configForHost(host);
    config.zoomFactor = std::clamp(factor, 0.8, 3.0);
    m_siteConfigs.insert(host, config);
    save();
    emit siteConfigChanged(host);
}

void SiteConfigManager::setMinimumFontSizeForUrl(const QString &url, int size)
{
    const QString host = hostFromUrl(url);
    if (host.isEmpty()) {
        setDefaultMinimumFontSize(size);
        return;
    }

    SiteConfig config = configForHost(host);
    config.minimumFontSize = std::clamp(size, 12, 28);
    m_siteConfigs.insert(host, config);
    save();
    emit siteConfigChanged(host);
}

void SiteConfigManager::setReadabilityModeForUrl(const QString &url, const QString &mode)
{
    const QString host = hostFromUrl(url);
    if (host.isEmpty()) {
        return;
    }

    SiteConfig config = configForHost(host);
    const double baseZoom = defaultZoomFactor();
    const int baseMinimumFontSize = defaultMinimumFontSize();
    config.readabilityMode = mode == QStringLiteral("large") || mode == QStringLiteral("standard")
        ? mode
        : QStringLiteral("sofa");
    config.zoomFactor = zoomFactorForReadabilityMode(config.readabilityMode, baseZoom);
    config.minimumFontSize = minimumFontSizeForReadabilityMode(config.readabilityMode, baseMinimumFontSize);

    m_siteConfigs.insert(host, config);
    save();
    emit siteConfigChanged(host);
}

void SiteConfigManager::resetSite(const QString &url)
{
    const QString host = hostFromUrl(url);
    if (host.isEmpty() || !m_siteConfigs.contains(host)) {
        return;
    }

    m_siteConfigs.remove(host);
    save();
    emit siteConfigChanged(host);
}

void SiteConfigManager::save() const
{
    SettingsStore::instance().saveFrom(*this);
}

SiteConfig SiteConfigManager::configForHost(const QString &host) const
{
    SiteConfig config;
    config.zoomFactor = defaultZoomFactor();
    config.minimumFontSize = defaultMinimumFontSize();
    // Per-site zoom / min font are stored values (written by setZoomForUrl,
    // setMinimumFontSizeForUrl, or setReadabilityModeForUrl). Do not recompute
    // minimumFontSize from readabilityMode on read — that ignored explicit
    // per-site font overrides when opening page scope via applyReadability.
    if (!host.isEmpty() && m_siteConfigs.contains(host)) {
        config = m_siteConfigs.value(host);
    }
    return config;
}

double SiteConfigManager::automaticSystemUiScale() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    return automaticDesktopSystemUiScale(screen);
}

double SiteConfigManager::automaticAppUiScale() const
{
    const ViewingProfile profile = detectViewingProfile(QGuiApplication::primaryScreen());
    return automaticReadability(profile).appUiScale;
}

double SiteConfigManager::automaticDefaultZoomFactor() const
{
    const ViewingProfile profile = detectViewingProfile(QGuiApplication::primaryScreen());
    return automaticReadability(profile).pageZoom;
}

int SiteConfigManager::automaticDefaultMinimumFontSize() const
{
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen) {
        return 18;
    }

    const ViewingProfile profile = detectViewingProfile(screen);
    return automaticReadability(profile).minimumFontSize;
}

QString SiteConfigManager::hostFromUrl(const QString &url)
{
    const QUrl parsed = QUrl::fromUserInput(url.trimmed());
    return parsed.host().toLower();
}