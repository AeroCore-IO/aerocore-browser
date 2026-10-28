#include "cursorutils.h"

#include <QGuiApplication>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QScreen>
#include <QByteArray>
#include <QString>

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
#include <X11/Xlib.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
#endif

#include <QTimer>

#include <algorithm>
#include <cmath>

namespace {
constexpr qreal kReferenceCursorSize = 32.0;
constexpr qreal kReferenceScreenDiagonalPixels = 2202.9071700822983;
constexpr qreal kReferenceScreenDpi = 110.0;
constexpr qreal kMinCursorScale = 0.95;
constexpr qreal kMaxCursorScale = 2.60;
constexpr qreal kMinCursorScaleOverride = 0.50;
constexpr qreal kMaxCursorScaleOverride = 4.00;
constexpr int kMinCursorSize = 26;
constexpr int kMaxCursorSize = 84;

const QScreen *resolvedScreen(const QScreen *screen)
{
    return screen ? screen : QGuiApplication::primaryScreen();
}

bool tryReadPositiveIntEnv(const char *name, int &value)
{
    bool ok = false;
    const int parsed = QString::fromUtf8(qgetenv(name)).trimmed().toInt(&ok);
    if (!ok || parsed <= 0) {
        return false;
    }

    value = parsed;
    return true;
}

bool tryReadPositiveRealEnv(const char *name, qreal &value)
{
    bool ok = false;
    const qreal parsed = QString::fromUtf8(qgetenv(name)).trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(parsed) || parsed <= 0.0) {
        return false;
    }

    value = parsed;
    return true;
}

int cursorSizeOverride()
{
    static const int value = []() {
        int parsed = 0;
        if (tryReadPositiveIntEnv("AEROCORE_CURSOR_SIZE", parsed)) {
            return qBound(kMinCursorSize, parsed, kMaxCursorSize);
        }
        return 0;
    }();
    return value;
}

qreal cursorScaleOverride()
{
    static const qreal value = []() {
        qreal parsed = 0.0;
        if (tryReadPositiveRealEnv("AEROCORE_CURSOR_SCALE", parsed)) {
            return qBound(kMinCursorScaleOverride, parsed, kMaxCursorScaleOverride);
        }
        return 1.0;
    }();
    return value;
}

bool hasUsablePhysicalSize(const QScreen *screen)
{
    if (!screen) {
        return false;
    }

    const QSizeF physicalSize = screen->physicalSize();
    return physicalSize.width() > 10.0 && physicalSize.height() > 10.0;
}

qreal effectiveScreenDpi(const QScreen *screen)
{
    if (!screen) {
        return 96.0;
    }

    qreal dpi = screen->physicalDotsPerInch();
    if (!std::isfinite(dpi) || dpi < 10.0) {
        dpi = screen->logicalDotsPerInch();
    }
    if (!std::isfinite(dpi) || dpi < 10.0) {
        dpi = 96.0;
    }
    return dpi;
}

qreal effectiveDevicePixelRatio(const QScreen *screen)
{
    if (!screen) {
        return 1.0;
    }

    const qreal dpr = screen->devicePixelRatio();
    if (!std::isfinite(dpr) || dpr < 1.0) {
        return 1.0;
    }

    return dpr;
}

qreal screenResolutionScale(const QScreen *screen)
{
    if (!screen) {
        return 1.0;
    }

    const QRect geometry = screen->geometry();
    if (geometry.isEmpty()) {
        return 1.0;
    }

    const qreal diagonalPixels = std::hypot(qreal(geometry.width()), qreal(geometry.height()));
    if (diagonalPixels <= 0.0) {
        return 1.0;
    }

    return qBound(0.85, std::sqrt(diagonalPixels / kReferenceScreenDiagonalPixels), 2.05);
}

qreal screenDensityScale(const QScreen *screen)
{
    if (!screen) {
        return 1.0;
    }

    if (hasUsablePhysicalSize(screen)) {
        return qBound(0.90,
                      std::sqrt(effectiveScreenDpi(screen) / kReferenceScreenDpi),
                      2.40);
    }

    const qreal logicalDpi = screen->logicalDotsPerInch();
    if (std::isfinite(logicalDpi) && logicalDpi >= 10.0) {
        return qBound(0.90, std::sqrt(logicalDpi / 96.0), 2.20);
    }

    return effectiveDevicePixelRatio(screen);
}

qreal cursorScaleForScreen(const QScreen *screen)
{
    const QScreen *currentScreen = resolvedScreen(screen);
    if (!currentScreen) {
        return 1.0;
    }

    const qreal resolutionScale = screenResolutionScale(currentScreen);
    const qreal densityScale = screenDensityScale(currentScreen);
    const qreal compositorScale = qBound(1.0, effectiveDevicePixelRatio(currentScreen), 2.50);

    const qreal combinedScale = std::max({resolutionScale,
                                          densityScale,
                                          compositorScale,
                                          resolutionScale * 0.78 + densityScale * 0.22,
                                          densityScale * 0.82 + compositorScale * 0.18});

    return qBound(kMinCursorScale, combinedScale, kMaxCursorScale);
}

QPixmap buildAdaptiveArrowPixmap(int logicalSize, qreal devicePixelRatio)
{
    const QString cacheKey = QStringLiteral("%1:%2")
                                 .arg(logicalSize)
                                 .arg(qRound(devicePixelRatio * 100.0));
    static QHash<QString, QPixmap> cache;
    if (cache.contains(cacheKey)) {
        return cache.value(cacheKey);
    }

    const int rasterSize = qMax(16, qRound(logicalSize * qMax(devicePixelRatio, 1.0)));
    const qreal unit = rasterSize / 32.0;

    QPixmap pixmap(rasterSize, rasterSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setPen(Qt::NoPen);

    QPainterPath outerArrow;
    outerArrow.moveTo(2.6 * unit, 2.0 * unit);
    outerArrow.lineTo(2.7 * unit, 25.9 * unit);
    outerArrow.lineTo(8.7 * unit, 20.1 * unit);
    outerArrow.lineTo(12.5 * unit, 30.2 * unit);
    outerArrow.lineTo(16.8 * unit, 28.4 * unit);
    outerArrow.lineTo(13.1 * unit, 18.8 * unit);
    outerArrow.lineTo(20.5 * unit, 18.7 * unit);
    outerArrow.closeSubpath();

    QPainterPath innerArrow;
    innerArrow.moveTo(4.5 * unit, 4.7 * unit);
    innerArrow.lineTo(4.6 * unit, 21.8 * unit);
    innerArrow.lineTo(8.2 * unit, 18.4 * unit);
    innerArrow.lineTo(11.7 * unit, 27.5 * unit);
    innerArrow.lineTo(14.0 * unit, 26.6 * unit);
    innerArrow.lineTo(10.4 * unit, 17.1 * unit);
    innerArrow.lineTo(17.1 * unit, 17.1 * unit);
    innerArrow.closeSubpath();

    painter.setBrush(QColor(0, 0, 0, 240));
    painter.drawPath(outerArrow);

    painter.setBrush(QColor(255, 255, 255, 250));
    painter.drawPath(innerArrow);

    painter.setPen(QPen(QColor(0, 0, 0, 170), qMax(1.0, unit * 0.9), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(innerArrow);
    painter.end();

    pixmap.setDevicePixelRatio(qMax(devicePixelRatio, 1.0));
    cache.insert(cacheKey, pixmap);
    return pixmap;
}

CursorRenderMode resolveCursorRenderMode()
{
    const QByteArray rawMode = qgetenv("AEROCORE_CURSOR_MODE").trimmed().toLower();
    if (rawMode == "system" || rawMode == "native") {
        return CursorRenderMode::System;
    }

    return CursorRenderMode::CustomOverlay;
}
}

CursorRenderMode cursorRenderMode()
{
    return resolveCursorRenderMode();
}

bool useCustomCursorRendering()
{
    return cursorRenderMode() == CursorRenderMode::CustomOverlay;
}

bool useSystemCursorRendering()
{
    return cursorRenderMode() == CursorRenderMode::System;
}

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
Display *sharedX11Display()
{
    static Display *display = []() -> Display * {
        if (QGuiApplication::platformName() != QStringLiteral("xcb")) {
            return nullptr;
        }

        const QByteArray displayName = qgetenv("DISPLAY");
        return XOpenDisplay(displayName.isEmpty() ? nullptr : displayName.constData());
    }();
    return display;
}

namespace {
constexpr int kMaxSnapshotKeycode = 255;
constexpr int kSnapshotGroups = 4;
constexpr int kSnapshotLevels = 4;

struct X11KeymapSnapshot {
    bool valid = false;
    bool hasUnicode = false;
    // [keycode][group][level]
    KeySym syms[kMaxSnapshotKeycode + 1][kSnapshotGroups][kSnapshotLevels]{};
};

X11KeymapSnapshot &x11KeymapSnapshot()
{
    static X11KeymapSnapshot snapshot;
    return snapshot;
}

int &x11KeymapSnapshotGeneration()
{
    static int generation = 0;
    return generation;
}

void fillSnapshotFromDisplay(Display *display, X11KeymapSnapshot *out)
{
    *out = {};
    if (!display || !out) {
        return;
    }

    // Pull the *current* XKB map from the server onto this Display (which does not
    // process Qt's NewKeyboardNotify stream and would otherwise keep a stale cache).
    XkbDescPtr desc = XkbGetMap(display, XkbAllClientInfoMask, XkbUseCoreKbd);
    if (!desc) {
        return;
    }
    XkbGetUpdatedMap(display, XkbAllClientInfoMask, desc);

    const int minKc = std::max(int(desc->min_key_code), 0);
    const int maxKc = std::min(int(desc->max_key_code), kMaxSnapshotKeycode);
    for (int kc = minKc; kc <= maxKc; ++kc) {
        for (int group = 0; group < kSnapshotGroups; ++group) {
            for (int level = 0; level < kSnapshotLevels; ++level) {
                const KeySym ks = XkbKeycodeToKeysym(display, KeyCode(kc), group, level);
                out->syms[kc][group][level] = ks;
                if (ks != NoSymbol && ks != 0 && (ks & 0xff000000) == 0x01000000) {
                    out->hasUnicode = true;
                }
            }
        }
    }

    XkbFreeKeyboard(desc, XkbAllComponentsMask, True);
    out->valid = true;
}
} // namespace

void refreshX11KeymapSnapshot()
{
    Display *display = sharedX11Display();
    if (!display) {
        return;
    }

    X11KeymapSnapshot fetched;
    fillSnapshotFromDisplay(display, &fetched);
    if (!fetched.valid) {
        return;
    }

    if (!fetched.hasUnicode) {
        XSync(display, False);
        fillSnapshotFromDisplay(display, &fetched);
        if (!fetched.valid) {
            return;
        }
    }

    X11KeymapSnapshot &snap = x11KeymapSnapshot();

    if (fetched.hasUnicode) {
        snap = fetched;
        ++x11KeymapSnapshotGeneration();
        qInfo().noquote() << QStringLiteral(
            "X11KeymapSnapshot: captured Steam OSK unicode remap (gen=%1)")
                                 .arg(x11KeymapSnapshotGeneration());
        return;
    }

    if (snap.valid && snap.hasUnicode) {
        const int gen = x11KeymapSnapshotGeneration();
        qInfo().noquote() << QStringLiteral(
            "X11KeymapSnapshot: defer latin restore (keep unicode gen=%1)")
                                 .arg(gen);
        QTimer::singleShot(200, qApp, [gen]() {
            X11KeymapSnapshot &current = x11KeymapSnapshot();
            if (x11KeymapSnapshotGeneration() != gen) {
                return;
            }
            if (current.hasUnicode) {
                current = {};
                qInfo().noquote() << QStringLiteral(
                    "X11KeymapSnapshot: cleared sticky unicode after restore");
            }
        });
        return;
    }

    snap = fetched;
}

void clearX11UnicodeKeymapSnapshot()
{
    X11KeymapSnapshot &snap = x11KeymapSnapshot();
    if (snap.hasUnicode) {
        snap = {};
        ++x11KeymapSnapshotGeneration();
    }
}

bool x11KeysymFromSnapshot(unsigned keycode, unsigned state, KeySym *keysymOut)
{
    if (!keysymOut || keycode > unsigned(kMaxSnapshotKeycode)) {
        return false;
    }

    const X11KeymapSnapshot &snap = x11KeymapSnapshot();
    if (!snap.valid) {
        return false;
    }

    // Prefer Unicode-plane keysyms (Steam OSK CJK) across groups/levels.
    for (int group = 0; group < kSnapshotGroups; ++group) {
        for (int level = 0; level < kSnapshotLevels; ++level) {
            const KeySym ks = snap.syms[keycode][group][level];
            if (ks != NoSymbol && ks != 0 && (ks & 0xff000000) == 0x01000000) {
                *keysymOut = ks;
                return true;
            }
        }
    }

    if (snap.hasUnicode) {
        return false;
    }

    const int group = 0;
    const int level = (state & ShiftMask) ? 1 : 0;
    const KeySym ks = snap.syms[keycode][group][level];
    if (ks == NoSymbol || ks == 0) {
        return false;
    }
    *keysymOut = ks;
    return true;
}
#endif

int adaptiveArrowCursorSizeForScreen(const QScreen *screen)
{
    const int sizeOverride = cursorSizeOverride();
    if (sizeOverride > 0) {
        return sizeOverride;
    }

    const QScreen *currentScreen = resolvedScreen(screen);
    const qreal scale = cursorScaleForScreen(currentScreen) * cursorScaleOverride();
    return qBound(kMinCursorSize,
                  qRound(kReferenceCursorSize * scale),
                  kMaxCursorSize);
}

QPoint adaptiveArrowCursorHotspotForSize(int logicalSize)
{
    const int hotX = qRound(logicalSize * 0.09);
    const int hotY = qRound(logicalSize * 0.09);
    return QPoint(hotX, hotY);
}

QPixmap adaptiveArrowCursorPixmapForScreen(const QScreen *screen)
{
    const QScreen *currentScreen = resolvedScreen(screen);
    const int logicalSize = adaptiveArrowCursorSizeForScreen(currentScreen);
    const qreal devicePixelRatio = currentScreen ? currentScreen->devicePixelRatio() : 1.0;
    return buildAdaptiveArrowPixmap(logicalSize, devicePixelRatio);
}

QCursor adaptiveArrowCursorForScreen(const QScreen *screen)
{
    const QScreen *currentScreen = resolvedScreen(screen);
    const int logicalSize = adaptiveArrowCursorSizeForScreen(currentScreen);
    const QPixmap pixmap = adaptiveArrowCursorPixmapForScreen(currentScreen);
    const QPoint hotSpot = adaptiveArrowCursorHotspotForSize(logicalSize);
    return QCursor(pixmap, hotSpot.x(), hotSpot.y());
}