#include "bookmarkmodel.h"
#include "browsercontroller.h"
#include "browserview.h"
#include "cursorutils.h"
#include "historymodel.h"
#include "inputmanager.h"
#include "logging/applogger.h"
#include "logging/stallwatchdog.h"
#include "siteconfigmanager.h"
#include "setting/settingsstore.h"
#include "setting/settingscontroller.h"
#if defined(__linux__)
#include "xcbxi2crashguard.h"
#endif

#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QWidget>
#include "inputmanageradapter.h"
#include "maincontroller.h"
#include "updatecontroller.h"
#include "distributionprofile.h"
#include "memoryimageprovider.h"
#include "browsercontainerquickitem.h"
#include <QAbstractNativeEventFilter>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>
#include <QStyleFactory>
#include <QTimer>
#include <QTranslator>
#include <QWindow>

#ifndef AEROCORE_BROWSER_VERSION
#define AEROCORE_BROWSER_VERSION "0.1.0"
#endif

#if defined(__linux__)
#include <atomic>
#include <csignal>
#include <dlfcn.h>
#include <xcb/xproto.h>
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/XKBlib.h>
#include <X11/keysym.h>
// Xlib defines KeyPress/KeyRelease macros that clash with QEvent::KeyPress
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#endif
#endif

#include <include/cef_app.h>
#include <include/cef_browser.h>
#include <include/cef_command_line.h>
#include <include/cef_process_message.h>
#include <include/cef_render_process_handler.h>
#include <include/cef_v8.h>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace {
#if defined(__linux__)
volatile std::sig_atomic_t gTerminationSignal = 0;

extern "C" void aerocoreTerminationSignalHandler(int signalNumber)
{
    gTerminationSignal = signalNumber;
}

void installTerminationHandlers()
{
    std::signal(SIGINT, aerocoreTerminationSignalHandler);
    std::signal(SIGTERM, aerocoreTerminationSignalHandler);
    std::signal(SIGHUP, aerocoreTerminationSignalHandler);
}

bool suppressXcbKeyboardEnabled()
{
    const QString value = qEnvironmentVariable("AEROCORE_SUPPRESS_XCB_KEYBOARD").trimmed().toLower();
    return value == QStringLiteral("1")
           || value == QStringLiteral("true")
           || value == QStringLiteral("yes")
           || value == QStringLiteral("on");
}

struct X11KeyMapping {
    int qtKey = 0;
    QString text;
};

// Evdev/XFree86 US QWERTY (keycode = linux KEY_* + 8). Used to bypass Qt's
// QXcbKeyboard::handleKeyEvent → xkb_state_key_get_one_sym path, which SIGSEGVs
// after Steam OSK hotplug leaves xkb_state inconsistent on gamescope.
X11KeyMapping mapX11KeycodeUs(uint8_t detail, uint16_t state)
{
    const bool shift = (state & XCB_MOD_MASK_SHIFT) != 0;
    const bool caps = (state & XCB_MOD_MASK_LOCK) != 0;
    const bool upper = shift ^ caps;

    auto letter = [upper](char lower, int qtKey) -> X11KeyMapping {
        const char ch = upper ? static_cast<char>(lower - 'a' + 'A') : lower;
        return {qtKey, QString(QChar::fromLatin1(ch))};
    };
    auto shifted = [shift](char normal, char alt, int qtKeyNormal, int qtKeyShifted) -> X11KeyMapping {
        if (shift) {
            return {qtKeyShifted, QString(QChar::fromLatin1(alt))};
        }
        return {qtKeyNormal, QString(QChar::fromLatin1(normal))};
    };

    switch (detail) {
    case 9:
        return {Qt::Key_Escape, {}};
    case 10:
        return shifted('1', '!', Qt::Key_1, Qt::Key_Exclam);
    case 11:
        return shifted('2', '@', Qt::Key_2, Qt::Key_At);
    case 12:
        return shifted('3', '#', Qt::Key_3, Qt::Key_NumberSign);
    case 13:
        return shifted('4', '$', Qt::Key_4, Qt::Key_Dollar);
    case 14:
        return shifted('5', '%', Qt::Key_5, Qt::Key_Percent);
    case 15:
        return shifted('6', '^', Qt::Key_6, Qt::Key_AsciiCircum);
    case 16:
        return shifted('7', '&', Qt::Key_7, Qt::Key_Ampersand);
    case 17:
        return shifted('8', '*', Qt::Key_8, Qt::Key_Asterisk);
    case 18:
        return shifted('9', '(', Qt::Key_9, Qt::Key_ParenLeft);
    case 19:
        return shifted('0', ')', Qt::Key_0, Qt::Key_ParenRight);
    case 20:
        return shifted('-', '_', Qt::Key_Minus, Qt::Key_Underscore);
    case 21:
        return shifted('=', '+', Qt::Key_Equal, Qt::Key_Plus);
    case 22:
        return {Qt::Key_Backspace, {}};
    case 23:
        return {Qt::Key_Tab, QStringLiteral("\t")};
    case 24:
        return letter('q', Qt::Key_Q);
    case 25:
        return letter('w', Qt::Key_W);
    case 26:
        return letter('e', Qt::Key_E);
    case 27:
        return letter('r', Qt::Key_R);
    case 28:
        return letter('t', Qt::Key_T);
    case 29:
        return letter('y', Qt::Key_Y);
    case 30:
        return letter('u', Qt::Key_U);
    case 31:
        return letter('i', Qt::Key_I);
    case 32:
        return letter('o', Qt::Key_O);
    case 33:
        return letter('p', Qt::Key_P);
    case 34:
        return shifted('[', '{', Qt::Key_BracketLeft, Qt::Key_BraceLeft);
    case 35:
        return shifted(']', '}', Qt::Key_BracketRight, Qt::Key_BraceRight);
    case 36:
    case 104:
        return {Qt::Key_Return, QStringLiteral("\r")};
    case 38:
        return letter('a', Qt::Key_A);
    case 39:
        return letter('s', Qt::Key_S);
    case 40:
        return letter('d', Qt::Key_D);
    case 41:
        return letter('f', Qt::Key_F);
    case 42:
        return letter('g', Qt::Key_G);
    case 43:
        return letter('h', Qt::Key_H);
    case 44:
        return letter('j', Qt::Key_J);
    case 45:
        return letter('k', Qt::Key_K);
    case 46:
        return letter('l', Qt::Key_L);
    case 47:
        return shifted(';', ':', Qt::Key_Semicolon, Qt::Key_Colon);
    case 48:
        return shifted('\'', '"', Qt::Key_Apostrophe, Qt::Key_QuoteDbl);
    case 49:
        return shifted('`', '~', Qt::Key_QuoteLeft, Qt::Key_AsciiTilde);
    case 51:
        return shifted('\\', '|', Qt::Key_Backslash, Qt::Key_Bar);
    case 52:
        return letter('z', Qt::Key_Z);
    case 53:
        return letter('x', Qt::Key_X);
    case 54:
        return letter('c', Qt::Key_C);
    case 55:
        return letter('v', Qt::Key_V);
    case 56:
        return letter('b', Qt::Key_B);
    case 57:
        return letter('n', Qt::Key_N);
    case 58:
        return letter('m', Qt::Key_M);
    case 59:
        return shifted(',', '<', Qt::Key_Comma, Qt::Key_Less);
    case 60:
        return shifted('.', '>', Qt::Key_Period, Qt::Key_Greater);
    case 61:
        return shifted('/', '?', Qt::Key_Slash, Qt::Key_Question);
    case 65:
        return {Qt::Key_Space, QStringLiteral(" ")};
    case 111:
        return {Qt::Key_Up, {}};
    case 113:
        return {Qt::Key_Left, {}};
    case 114:
        return {Qt::Key_Right, {}};
    case 116:
        return {Qt::Key_Down, {}};
    case 119:
        return {Qt::Key_Delete, {}};
    default:
        return {};
    }
}

Qt::KeyboardModifiers qtModifiersFromXcbState(uint16_t state)
{
    Qt::KeyboardModifiers mods;
    if (state & XCB_MOD_MASK_SHIFT) {
        mods |= Qt::ShiftModifier;
    }
    if (state & XCB_MOD_MASK_CONTROL) {
        mods |= Qt::ControlModifier;
    }
    if (state & XCB_MOD_MASK_1) {
        mods |= Qt::AltModifier;
    }
    if (state & XCB_MOD_MASK_4) {
        mods |= Qt::MetaModifier;
    }
    return mods;
}

// Prefer a fresh server-side keymap fetch (includes Steam OSK temporary Unicode remaps).
// Never use Qt's xkb_state — that path SIGSEGVs after OSK hotplug.
// Also avoid shared Display's stale XKB *client* cache (XLookupString / XkbKeycodeToKeysym):
// that connection does not process NewKeyboardNotify, so it can keep an old remap
X11KeyMapping mappingFromKeysym(KeySym keysym)
{
    if (keysym == NoSymbol || keysym == 0) {
        return {};
    }

    QString text;
    // Direct Unicode keysyms (Steam OSK remaps CJK candidates this way)
    if ((keysym & 0xff000000) == 0x01000000) {
        const char32_t cp = static_cast<char32_t>(keysym & 0x00ffffff);
        if (cp != 0) {
            text = QString::fromUcs4(&cp, 1);
        }
    } else if (keysym >= 0x20 && keysym <= 0xff && keysym != 0x7f) {
        text = QString(QChar(static_cast<ushort>(keysym)));
    }

    int qtKey = 0;
    if (text.size() == 1) {
        const ushort uc = text.at(0).unicode();
        if (uc >= 'a' && uc <= 'z') {
            qtKey = Qt::Key_A + (uc - 'a');
        } else if (uc >= 'A' && uc <= 'Z') {
            qtKey = Qt::Key_A + (uc - 'A');
        } else if (uc >= '0' && uc <= '9') {
            qtKey = Qt::Key_0 + (uc - '0');
        } else if (uc == ' ') {
            qtKey = Qt::Key_Space;
        } else if (uc >= 0x20 && uc < 0x80) {
            qtKey = int(uc);
        } else {
            qtKey = Qt::Key_unknown;
        }
    } else if (!text.isEmpty()) {
        qtKey = Qt::Key_unknown;
    } else {
        switch (keysym) {
        case XK_BackSpace:
            qtKey = Qt::Key_Backspace;
            break;
        case XK_Return:
        case XK_KP_Enter:
            qtKey = Qt::Key_Return;
            break;
        case XK_Escape:
            qtKey = Qt::Key_Escape;
            break;
        case XK_Delete:
        case XK_KP_Delete:
            qtKey = Qt::Key_Delete;
            break;
        case XK_Tab:
        case XK_ISO_Left_Tab:
            qtKey = Qt::Key_Tab;
            break;
        default:
            break;
        }
    }

    if (qtKey == 0 && text.isEmpty()) {
        return {};
    }
    return {qtKey == 0 ? int(Qt::Key_unknown) : qtKey, text};
}

X11KeyMapping mapX11KeycodeFromServer(uint8_t detail, uint16_t state)
{
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    Display *display = sharedX11Display();
    if (!display) {
        return {};
    }

    KeySym keysym = NoSymbol;
    // 1) Snapshot taken at NewKeyboardNotify — matches Steam remap at commit time
    if (!x11KeysymFromSnapshot(detail, state, &keysym)) {
        // 2) Fallback: live server query (may already be restored to Latin)
        int keysymsPerKeycode = 0;
        KeySym *keysyms = XGetKeyboardMapping(display, KeyCode(detail), 1, &keysymsPerKeycode);
        if (keysyms && keysymsPerKeycode > 0) {
            for (int i = 0; i < keysymsPerKeycode; ++i) {
                const KeySym ks = keysyms[i];
                if (ks != NoSymbol && ks != 0 && (ks & 0xff000000) == 0x01000000) {
                    keysym = ks;
                    break;
                }
            }
            if (keysym == NoSymbol || keysym == 0) {
                const int level = ((state & ShiftMask) && keysymsPerKeycode > 1) ? 1 : 0;
                keysym = keysyms[level];
            }
        }
        if (keysyms) {
            XFree(keysyms);
        }
    }

    X11KeyMapping mapped = mappingFromKeysym(keysym);
    if (!mapped.text.isEmpty() && mapped.qtKey == Qt::Key_unknown) {
        static int s_cjkMapLogBudget = 16;
        if (s_cjkMapLogBudget > 0) {
            --s_cjkMapLogBudget;
            qInfo().noquote() << QStringLiteral(
                "InputManager: keymap keycode=%1 → text=%2 keysym=0x%3")
                                     .arg(detail)
                                     .arg(mapped.text)
                                     .arg(quint32(keysym), 0, 16);
        }
    }
    return mapped;
#else
    Q_UNUSED(detail);
    Q_UNUSED(state);
    return {};
#endif
}

bool bypassBrokenXkbKeyboard()
{
    if (!qApp) {
        return false;
    }
    if (qApp->property("aerocoreTextInputActive").toBool()) {
        return true;
    }
    const qint64 graceUntil = qApp->property("aerocoreTextInputGraceUntilMs").toLongLong();
    return graceUntil > 0 && QDateTime::currentMSecsSinceEpoch() < graceUntil;
}

class XcbKeyboardSuppressor final : public QAbstractNativeEventFilter
{
public:
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *) override
    {
        if (eventType != QByteArrayLiteral("xcb_generic_event_t") || !message) {
            return false;
        }

        auto *event = static_cast<xcb_generic_event_t *>(message);
        const uint8_t responseType = event->response_type & ~0x80;
        if (responseType != XCB_KEY_PRESS && responseType != XCB_KEY_RELEASE) {
            return false;
        }

        const auto *keyEvent = static_cast<xcb_key_press_event_t *>(message);
        const bool textSession = bypassBrokenXkbKeyboard();

        // QXcbKeyboard::handleKeyPressEvent -> xkb_state_key_get_one_sym.
        if (textSession) {
            X11KeyMapping mapped = mapX11KeycodeFromServer(keyEvent->detail, keyEvent->state);
            if (responseType == XCB_KEY_PRESS && !mapped.text.isEmpty()
                && mapped.text.size() == 1 && mapped.text.at(0).unicode() < 0x80
                && mapped.text.at(0).isLetter()) {
                refreshX11KeymapSnapshot();
                const X11KeyMapping retry =
                    mapX11KeycodeFromServer(keyEvent->detail, keyEvent->state);
                if (!retry.text.isEmpty()
                    && (retry.qtKey == Qt::Key_unknown || retry.text.at(0).unicode() >= 0x80)) {
                    mapped = retry;
                    qInfo().noquote() << QStringLiteral(
                        "XcbKeyboardSuppressor: latin→unicode retry keycode=%1 text=%2")
                                             .arg(keyEvent->detail)
                                             .arg(mapped.text);
                }
            }
            if (mapped.qtKey == 0) {
                mapped = mapX11KeycodeUs(keyEvent->detail, keyEvent->state);
            }
            if (mapped.qtKey == 0) {
                static int s_unknownKeyLogBudget = 8;
                if (s_unknownKeyLogBudget > 0) {
                    --s_unknownKeyLogBudget;
                    qInfo().noquote()
                        << QStringLiteral(
                               "XcbKeyboardSuppressor: swallow unmapped keycode=%1 state=0x%2")
                               .arg(keyEvent->detail)
                               .arg(keyEvent->state, 0, 16);
                }
                return true; // swallow unknown keys; never enter broken xkb path
            }

            if (responseType == XCB_KEY_PRESS && !mapped.text.isEmpty()
                && mapped.qtKey == Qt::Key_unknown) {
                static int s_unicodeKeyLogBudget = 16;
                if (s_unicodeKeyLogBudget > 0) {
                    --s_unicodeKeyLogBudget;
                    qInfo().noquote()
                        << QStringLiteral(
                               "XcbKeyboardSuppressor: unicode from X11 keycode=%1 text=%2")
                               .arg(keyEvent->detail)
                               .arg(mapped.text);
                }
            }

            QObject *target = QGuiApplication::focusObject();
            if (!target) {
                target = QApplication::activeWindow();
            }
            if (!target) {
                target = qApp;
            }

            const QString eventText = (responseType == XCB_KEY_PRESS) ? mapped.text : QString();
            QCoreApplication::postEvent(
                target,
                new QKeyEvent(responseType == XCB_KEY_PRESS ? QEvent::KeyPress : QEvent::KeyRelease,
                              mapped.qtKey,
                              qtModifiersFromXcbState(keyEvent->state),
                              eventText));
            return true;
        }

        // Outside text sessions: only remap when AEROCORE_SUPPRESS_XCB_KEYBOARD is on.
        if (!suppressXcbKeyboardEnabled()) {
            return false;
        }

        if (shouldBypassSuppression(*keyEvent)) {
            return false;
        }

        const int qtKey = qtKeyForNativeDetail(keyEvent->detail);
        if (qtKey == 0) {
            return true;
        }

        QObject *target = QGuiApplication::focusObject();
        if (!target) {
            target = QApplication::activeWindow();
        }
        if (!target) {
            target = qApp;
        }

        QCoreApplication::postEvent(
            target,
            new QKeyEvent(responseType == XCB_KEY_PRESS ? QEvent::KeyPress : QEvent::KeyRelease,
                          qtKey,
                          Qt::NoModifier));
        return true;
    }

private:
    static bool shouldBypassSuppression(const xcb_key_press_event_t &event)
    {
        if ((event.state & XCB_MOD_MASK_CONTROL) != 0) {
            return true;
        }

        switch (event.detail) {
        case 37:
        case 105:
            return true;
        default:
            return false;
        }
    }

    static int qtKeyForNativeDetail(uint8_t detail)
    {
        switch (detail) {
        case 9:
            return Qt::Key_Escape;
        case 25:
            return Qt::Key_W;
        case 38:
            return Qt::Key_A;
        case 39:
            return Qt::Key_S;
        case 40:
            return Qt::Key_D;
        case 36:
        case 104:
            return Qt::Key_Return;
        case 111:
            return Qt::Key_Up;
        case 113:
            return Qt::Key_Left;
        case 114:
            return Qt::Key_Right;
        case 116:
            return Qt::Key_Down;
        default:
            return 0;
        }
    }
};
#endif

QString qtScaleSource()
{
    if (qEnvironmentVariableIsSet("QT_SCALE_FACTOR")) {
        return QStringLiteral("QT_SCALE_FACTOR");
    }
    if (qEnvironmentVariableIsSet("QT_SCREEN_SCALE_FACTORS")) {
        return QStringLiteral("QT_SCREEN_SCALE_FACTORS");
    }
    if (qEnvironmentVariableIsSet("QT_FONT_DPI")) {
        return QStringLiteral("QT_FONT_DPI");
    }
    return QStringLiteral("system");
}

void logQtScaleState(const QString &reason, const QWidget *widget = nullptr)
{
    const QScreen *screen = widget ? widget->screen() : QGuiApplication::primaryScreen();
    if (!screen) {
        qInfo().noquote() << QStringLiteral("Qt UI scale [%1]: no active screen").arg(reason);
        return;
    }

    const qreal windowScale = widget ? widget->devicePixelRatioF() : screen->devicePixelRatio();
    qInfo().noquote()
        << QStringLiteral("Qt UI scale [%1]: source=%2 screen=%3 windowScale=%4 screenScale=%5 logicalDpi=%6 physicalDpi=%7 geometry=%8x%9")
               .arg(reason,
                    qtScaleSource(),
                    screen->name().isEmpty() ? QStringLiteral("<unnamed>") : screen->name(),
                    QString::number(windowScale, 'f', 2),
                    QString::number(screen->devicePixelRatio(), 'f', 2),
                    QString::number(screen->logicalDotsPerInch(), 'f', 2),
                    QString::number(screen->physicalDotsPerInch(), 'f', 2),
                    QString::number(screen->geometry().width()),
                    QString::number(screen->geometry().height()));
}

namespace {

SiteConfigManager *g_siteConfigForStartup = nullptr;

} // namespace

QString desktopUserAgent()
{
    if (g_siteConfigForStartup) {
        return g_siteConfigForStartup->desktopUserAgent();
    }

    return SiteConfigManager().desktopUserAgent();
}

bool videoPlaybackTrackingEnabled()
{
    return true;
}

QString gpuMode()
{
    return SettingsStore::resolveGpuMode();
}

QString chromiumDisableFeatures(const QString &mode)
{
    QStringList features{
        QStringLiteral("UserAgentClientHint"),
        QStringLiteral("UserAgentClientHintsGREASEUpdate"),
        QStringLiteral("IsolateOrigins"),
        QStringLiteral("site-per-process")
    };

#if defined(__linux__)
    if (mode == QStringLiteral("x11-egl")) {
        features.append(QStringLiteral("Vulkan"));
    }
#endif

    return features.join(QLatin1Char(','));
}

QString gpuDiagnosticsScript()
{
    return QStringLiteral(R"JS((function() {
    if (window.__aerocoreGpuDiagnosticsInstalled) {
        return;
    }
    window.__aerocoreGpuDiagnosticsInstalled = true;

    const classifyBackend = (vendor, renderer) => {
        const text = String((vendor || '') + ' ' + (renderer || '')).toLowerCase();
        if (text.includes('swiftshader')) {
            return 'swiftshader';
        }
        if (text.includes('llvmpipe')) {
            return 'llvmpipe';
        }
        if (text.includes('angle')) {
            if (text.includes('vulkan')) {
                return 'angle-vulkan';
            }
            if (text.includes('opengl es')) {
                return 'angle-gles';
            }
            if (text.includes('opengl')) {
                return 'angle-gl';
            }
            return 'angle';
        }
        if (text.includes('vulkan')) {
            return 'vulkan';
        }
        if (text.includes('mesa')) {
            return 'mesa';
        }
        return 'native-gl';
    };

    const report = () => {
        try {
            if (typeof window.__aerocoreReportGpuDiagnostics !== 'function') {
                return;
            }

            const canvas = document.createElement('canvas');
            const gl = canvas.getContext('webgl2', { powerPreference: 'high-performance' })
                || canvas.getContext('webgl', { powerPreference: 'high-performance' })
                || canvas.getContext('experimental-webgl');
            if (!gl) {
                window.__aerocoreReportGpuDiagnostics('webgl-unavailable', '', '');
                return;
            }

            let vendor = '';
            let renderer = '';
            try {
                const debugInfo = gl.getExtension('WEBGL_debug_renderer_info');
                if (debugInfo) {
                    vendor = String(gl.getParameter(debugInfo.UNMASKED_VENDOR_WEBGL) || '');
                    renderer = String(gl.getParameter(debugInfo.UNMASKED_RENDERER_WEBGL) || '');
                }
            } catch (e) {
            }

            if (!vendor) {
                try {
                    vendor = String(gl.getParameter(gl.VENDOR) || '');
                } catch (e) {
                }
            }
            if (!renderer) {
                try {
                    renderer = String(gl.getParameter(gl.RENDERER) || '');
                } catch (e) {
                }
            }

            window.__aerocoreReportGpuDiagnostics(classifyBackend(vendor, renderer), vendor, renderer);
        } catch (e) {
            try {
                window.__aerocoreReportGpuDiagnostics('probe-error', '', String((e && e.message) || e || 'unknown'));
            } catch (ignored) {
            }
        }
    };

    if (document.readyState === 'loading') {
        window.addEventListener('DOMContentLoaded', report, { once: true });
    } else {
        report();
    }
})();
)JS");
}

QString videoPlaybackTrackingScript()
{
    return QStringLiteral(R"JS((function() {
    if (window.__aerocoreVideoPlaybackTrackerInstalled) {
        return;
    }
    window.__aerocoreVideoPlaybackTrackerInstalled = true;

    const previewGraceMs = 1500;
    const trackedVideos = new Map();

    const isVideoElement = (value) => {
        return typeof HTMLVideoElement !== 'undefined' && value instanceof HTMLVideoElement;
    };

    const clearConfirmTimer = (state) => {
        if (!state || !state.confirmTimer) {
            return;
        }
        clearTimeout(state.confirmTimer);
        state.confirmTimer = 0;
    };

    const removeTrackedVideo = (video) => {
        const state = trackedVideos.get(video);
        if (!state) {
            return;
        }
        clearConfirmTimer(state);
        trackedVideos.delete(video);
    };

    const touchTrackedVideo = (video) => {
        if (!isVideoElement(video)) {
            return null;
        }

        let state = trackedVideos.get(video);
        if (!state) {
            state = {
                startedAt: performance.now(),
                confirmTimer: 0
            };
            trackedVideos.set(video, state);
        }
        return state;
    };

    const isVideoPlaying = (video) => {
        try {
            return !!video && !video.paused && !video.ended && video.readyState >= 2;
        } catch (e) {
            return false;
        }
    };

    const fullscreenHasVideo = () => {
        try {
            const fullscreenElement = document.fullscreenElement;
            return !!fullscreenElement
                && (fullscreenElement.tagName === 'VIDEO'
                    || (typeof fullscreenElement.querySelector === 'function' && !!fullscreenElement.querySelector('video')));
        } catch (e) {
            return false;
        }
    };

    const reportToNative = () => {
        try {
            if (typeof window.__aerocoreReportVideoPlayback !== 'function') {
                return;
            }

            const fullscreenVideoActive = fullscreenHasVideo();

            const pictureInPictureActive = (() => {
                try {
                    return !!document.pictureInPictureElement;
                } catch (e) {
                    return false;
                }
            })();

            let active = false;
            const now = performance.now();
            for (const [video, state] of Array.from(trackedVideos.entries())) {
                if (!video || !video.isConnected || !isVideoPlaying(video)) {
                    removeTrackedVideo(video);
                    continue;
                }

                const mutedShortPreview = video.muted
                    && (now - state.startedAt) < previewGraceMs
                    && !pictureInPictureActive
                    && !fullscreenVideoActive;

                if (mutedShortPreview) {
                    if (!state.confirmTimer) {
                        state.confirmTimer = window.setTimeout(() => {
                            state.confirmTimer = 0;
                            scheduleReport();
                        }, Math.max(1, previewGraceMs - (now - state.startedAt)));
                    }
                    continue;
                }

                clearConfirmTimer(state);
                active = true;
                break;
            }

            if (reportToNative.lastActive === active
                && reportToNative.lastPictureInPicture === pictureInPictureActive
                && reportToNative.lastFullscreen === fullscreenVideoActive) {
                return;
            }

            reportToNative.lastActive = active;
            reportToNative.lastPictureInPicture = pictureInPictureActive;
            reportToNative.lastFullscreen = fullscreenVideoActive;
            window.__aerocoreReportVideoPlayback(active, pictureInPictureActive, fullscreenVideoActive);
        } catch (e) {
        }
    };
    reportToNative.lastActive = null;
    reportToNative.lastPictureInPicture = null;
    reportToNative.lastFullscreen = null;

    let reportTimer = 0;
    const scheduleReport = () => {
        if (reportTimer) {
            return;
        }
        reportTimer = window.setTimeout(() => {
            reportTimer = 0;
            reportToNative();
        }, 120);
    };

    const observedEvents = [
        'play',
        'playing',
        'pause',
        'ended',
        'emptied',
        'abort',
        'seeked',
        'volumechange',
        'enterpictureinpicture',
        'leavepictureinpicture',
        'fullscreenchange',
        'webkitbeginfullscreen',
        'webkitendfullscreen'
    ];

    const handleMediaEvent = (event) => {
        const target = event ? event.target : null;
        if (isVideoElement(target)) {
            if (event.type === 'pause' || event.type === 'ended' || event.type === 'emptied' || event.type === 'abort') {
                removeTrackedVideo(target);
            } else {
                touchTrackedVideo(target);
            }
        }

        scheduleReport();
    };

    for (const eventName of observedEvents) {
        document.addEventListener(eventName, handleMediaEvent, true);
    }

    window.addEventListener('pagehide', () => {
        if (reportTimer) {
            clearTimeout(reportTimer);
            reportTimer = 0;
        }
        for (const state of trackedVideos.values()) {
            clearConfirmTimer(state);
        }
        trackedVideos.clear();
        try {
            reportToNative.lastActive = false;
            reportToNative.lastPictureInPicture = false;
            reportToNative.lastFullscreen = false;
            window.__aerocoreReportVideoPlayback(false, false, false);
        } catch (e) {
        }
    }, { once: true });

    scheduleReport();
})();
)JS");
}

constexpr char kVideoPlaybackProcessMessage[] = "AeroCore.VideoPlayback";
constexpr char kGpuDiagnosticsProcessMessage[] = "AeroCore.GpuDiagnostics";

class MediaPlaybackReporter final : public CefV8Handler
{
public:
    bool Execute(const CefString &name,
                 CefRefPtr<CefV8Value>,
                 const CefV8ValueList &arguments,
                 CefRefPtr<CefV8Value> &,
                 CefString &exception) override
    {
        if (name != "aerocoreReportVideoPlayback") {
            return false;
        }

        if (arguments.size() < 1 || arguments.size() > 3 || !arguments[0]->IsBool()) {
            exception = "aerocoreReportVideoPlayback expects 1 to 3 boolean arguments";
            return true;
        }

        CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
        if (!context) {
            return true;
        }

        CefRefPtr<CefFrame> frame = context->GetFrame();
        if (!frame) {
            return true;
        }

        CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kVideoPlaybackProcessMessage);
        message->GetArgumentList()->SetBool(0, arguments[0]->GetBoolValue());
        message->GetArgumentList()->SetBool(1, arguments.size() > 1 && arguments[1]->IsBool() ? arguments[1]->GetBoolValue() : false);
        message->GetArgumentList()->SetBool(2, arguments.size() > 2 && arguments[2]->IsBool() ? arguments[2]->GetBoolValue() : false);
        frame->SendProcessMessage(PID_BROWSER, message);
        return true;
    }

private:
    IMPLEMENT_REFCOUNTING(MediaPlaybackReporter);
};

class GpuDiagnosticsReporter final : public CefV8Handler
{
public:
    bool Execute(const CefString &name,
                 CefRefPtr<CefV8Value>,
                 const CefV8ValueList &arguments,
                 CefRefPtr<CefV8Value> &,
                 CefString &exception) override
    {
        if (name != "aerocoreReportGpuDiagnostics") {
            return false;
        }

        if (arguments.empty() || arguments.size() > 3 || !arguments[0]->IsString()) {
            exception = "aerocoreReportGpuDiagnostics expects 1 to 3 string arguments";
            return true;
        }

        CefRefPtr<CefV8Context> context = CefV8Context::GetCurrentContext();
        if (!context) {
            return true;
        }

        CefRefPtr<CefFrame> frame = context->GetFrame();
        if (!frame) {
            return true;
        }

        CefRefPtr<CefProcessMessage> message = CefProcessMessage::Create(kGpuDiagnosticsProcessMessage);
        message->GetArgumentList()->SetString(0, arguments[0]->GetStringValue());
        message->GetArgumentList()->SetString(1, arguments.size() > 1 && arguments[1]->IsString() ? arguments[1]->GetStringValue() : CefString());
        message->GetArgumentList()->SetString(2, arguments.size() > 2 && arguments[2]->IsString() ? arguments[2]->GetStringValue() : CefString());
        frame->SendProcessMessage(PID_BROWSER, message);
        return true;
    }

private:
    IMPLEMENT_REFCOUNTING(GpuDiagnosticsReporter);
};

void applyGpuMode(const QString &mode, CefRefPtr<CefCommandLine> commandLine)
{
    if (mode == QStringLiteral("auto")) {
        return;
    }

    if (mode == QStringLiteral("unsafe")) {
        commandLine->AppendSwitch("ignore-gpu-blocklist");
        return;
    }

#if defined(__linux__)
    if (mode == QStringLiteral("x11-egl")) {
        commandLine->AppendSwitchWithValue("use-gl", "egl");
        return;
    }
#endif

    commandLine->AppendSwitch("disable-gpu");
    commandLine->AppendSwitch("disable-gpu-compositing");

#if defined(_WIN32)
    commandLine->AppendSwitch("disable-direct-composition");
#endif
}

#if defined(__linux__)
// ---------------------------------------------------------------------------
// Adaptive GLib poll timeout guard for CEF's idle main-loop spin.
//
// CEF's base::MessagePumpGlib adds a WorkSource to Qt's default GLib context. Since Qt owns the
// event loop, MessagePumpGlib::Run() is never called and its state_ remains null. The source then
// repeatedly reports a zero timeout, causing g_main_context_iteration to busy-poll at about
// 80,000 iterations per second and consume 100% CPU on the main thread.
//
// A fixed minimum timeout would affect every source and cannot distinguish idle WorkSource polls
// from Qt work that needs a fast response. A large minimum such as 16 ms delays the 10 ms CEF pump
// and Qt event processing, which can leave the page black.
//
// Raise the timeout only after consecutive zero-timeout polls with no ready file descriptors,
// which indicate a pure spin. Resume normal polling immediately when a descriptor is ready.
// Keep the raised timeout far below the 10 ms pump interval: OSR frames arrive via direct calls,
// not file descriptors, so an excessive timeout (such as 100 ms) can starve the compositor. The
// 2 ms default preserves 10–12 ms pump intervals while reducing idle polling to about 500/s.
//
// This guard relies on the fixed 10 ms CefDoMessageLoopWork() timer below to process CEF work.
// Do not switch to external_message_pump: OnScheduleMessagePumpWork is rarely called after startup
// in this build, which would starve CEF and leave pages black or unable to load.
// ---------------------------------------------------------------------------
using GlibPollFunc = int (*)(void *fds, unsigned int nfds, int timeout);
static GlibPollFunc g_prevGlibPoll = nullptr;

// Consecutive idle polls with zero timeout and no ready descriptors.
static std::atomic<int> g_glibIdleCount{0};

// Idle wait after a spin is confirmed, configurable with AEROCORE_GLIB_IDLE_MS (default: 2 ms).
// Keep it well below the 10 ms pump interval to avoid delaying CefDoMessageLoopWork().
static int aerocoreGlibIdleMs()
{
    static const int value = [] {
        bool ok = false;
        const int v = qEnvironmentVariableIntValue("AEROCORE_GLIB_IDLE_MS", &ok);
        return (ok && v > 0) ? v : 2;
    }();
    return value;
}

static int aerocoreGlibPoll(void *fds, unsigned int nfds, int timeout)
{
    // Intercept only zero-timeout polls; pass through infinite and positive timeouts unchanged.
    if (timeout == 0) {
        const int ready = g_prevGlibPoll(fds, nfds, 0);
        if (ready > 0) {
            // A ready descriptor indicates real activity; reset the counter without adding latency.
            g_glibIdleCount.store(0, std::memory_order_relaxed);
            return ready;
        }
        // Raise the timeout only after enough consecutive idle polls confirm a pure spin.
        const int idle = g_glibIdleCount.fetch_add(1, std::memory_order_relaxed) + 1;
        if (idle > 3)
            return g_prevGlibPoll(fds, nfds, aerocoreGlibIdleMs());
        return ready;
    }
    // A nonzero timeout means a regular source such as a Qt/CEF timer is waiting.
    g_glibIdleCount.store(0, std::memory_order_relaxed);
    return g_prevGlibPoll(fds, nfds, timeout);
}

static void installCefSpinGuard()
{
    {
        bool ok = false;
        const int v = qEnvironmentVariableIntValue("AEROCORE_SPIN_GUARD", &ok);
        if (ok && v == 0) {
            qInfo().noquote() << QStringLiteral("[spin-guard] DISABLED via AEROCORE_SPIN_GUARD=0");
            return;
        }
    }
    using GetPollFn = GlibPollFunc (*)(void *);
    using SetPollFn = void (*)(void *, GlibPollFunc);
    auto getPoll = reinterpret_cast<GetPollFn>(dlsym(RTLD_DEFAULT, "g_main_context_get_poll_func"));
    auto setPoll = reinterpret_cast<SetPollFn>(dlsym(RTLD_DEFAULT, "g_main_context_set_poll_func"));
    if (!getPoll || !setPoll) {
        qWarning().noquote() << QStringLiteral("[spin-guard] glib poll funcs unavailable; guard NOT installed");
        return;
    }
    g_prevGlibPoll = getPoll(nullptr);
    if (!g_prevGlibPoll) {
        qWarning().noquote() << QStringLiteral("[spin-guard] no existing poll func; guard NOT installed");
        return;
    }
    setPoll(nullptr, &aerocoreGlibPoll);
    qInfo().noquote() << QStringLiteral("[spin-guard] installed adaptive idle guard (idle -> %1 ms)")
                             .arg(aerocoreGlibIdleMs());
}
#endif // __linux__

static QString locateWidevineCdmDir(const QDir &executableDir)
{
    const QStringList candidates = {
        qEnvironmentVariable("AEROCORE_WIDEVINE_DIR"),
        executableDir.absoluteFilePath(QStringLiteral("WidevineCdm")),
    };
    for (const QString &candidate : candidates) {
        if (candidate.isEmpty()) {
            continue;
        }
        const QDir cdmDir(candidate);
        const bool valid = cdmDir.exists(QStringLiteral("manifest.json"))
            && QFileInfo::exists(cdmDir.filePath(
                QStringLiteral("_platform_specific/linux_x64/libwidevinecdm.so")));
        if (valid) {
            return cdmDir.absolutePath();
        }
    }
    return QString();
}

static void prepareWidevineCdmHintFile(const QString &cacheRoot, const QDir &executableDir)
{
    const QString cdmDir = locateWidevineCdmDir(executableDir);
    if (cdmDir.isEmpty()) {
        qWarning().noquote() << QStringLiteral(
            "Widevine CDM not found (expected <exe dir>/WidevineCdm or AEROCORE_WIDEVINE_DIR); "
            "DRM playback will be unavailable.");
        return;
    }

    const QDir hintDir(QDir(cacheRoot).filePath(QStringLiteral("WidevineCdm")));
    if (!hintDir.mkpath(QStringLiteral("."))) {
        qWarning().noquote() << QStringLiteral("Failed to create Widevine hint dir: %1").arg(hintDir.absolutePath());
        return;
    }

    QJsonObject hint;
    hint.insert(QStringLiteral("Path"), cdmDir);

    QSaveFile hintFile(hintDir.filePath(QStringLiteral("latest-component-updated-widevine-cdm")));
    if (!hintFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
        || hintFile.write(QJsonDocument(hint).toJson(QJsonDocument::Compact)) < 0
        || !hintFile.commit()) {
        qWarning().noquote() << QStringLiteral("Failed to write Widevine hint file: %1").arg(hintFile.fileName());
        return;
    }
    qInfo().noquote() << QStringLiteral("Widevine CDM registered via hint file: %1 -> %2")
                             .arg(hintFile.fileName(), cdmDir);
}

static void startWidevinePreparationAfterUiLoaded(const QString &executableDir)
{
    const QString helper = qEnvironmentVariable("AEROCORE_WIDEVINE_HELPER").trimmed();
    if (helper.isEmpty() || !QFileInfo(helper).isExecutable()) {
        return;
    }

    const QString outputDir = qEnvironmentVariable(
        "AEROCORE_WIDEVINE_DIR",
        QDir(executableDir).filePath(QStringLiteral("WidevineCdm")));
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("WIDEVINE_OUT_DIR"), outputDir);

    QProcess process;
    process.setProgram(helper);
    process.setProcessEnvironment(environment);
    if (process.startDetached()) {
        qInfo().noquote() << QStringLiteral(
            "Widevine preparation started after UI load: %1 -> %2").arg(helper, outputDir);
    } else {
        qWarning().noquote() << QStringLiteral(
            "Failed to start deferred Widevine preparation: %1").arg(helper);
    }
}

class AeroCoreCefApp final : public CefApp, public CefRenderProcessHandler
{
public:
    void OnBeforeCommandLineProcessing(const CefString &, CefRefPtr<CefCommandLine> commandLine) override
    {
        const QString selectedGpuMode = gpuMode();
        const std::string userAgent = desktopUserAgent().toStdString();
        commandLine->AppendSwitchWithValue("user-agent", userAgent);
        const QString language = distributionProfile().value(QStringLiteral("language"))
                                     .toString(QStringLiteral("zh-CN"));
        commandLine->AppendSwitchWithValue("lang", language.toStdString());
        commandLine->AppendSwitchWithValue("autoplay-policy", "no-user-gesture-required");
        commandLine->AppendSwitch("no-first-run");
        commandLine->AppendSwitch("no-default-browser-check");
        commandLine->AppendSwitch("enable-logging");
        commandLine->AppendSwitchWithValue("v", "1");
        commandLine->AppendSwitchWithValue(
            "enable-features",
            "OverlayScrollbar,OverlayScrollbarFlashAfterAnyScrollUpdate");
        commandLine->AppendSwitchWithValue(
            "disable-features",
            chromiumDisableFeatures(selectedGpuMode).toStdString());
        commandLine->AppendSwitch("disable-site-isolation-trials");
        commandLine->AppendSwitch("enable-media-stream");
        applyGpuMode(selectedGpuMode, commandLine);
    }

    CefRefPtr<CefRenderProcessHandler> GetRenderProcessHandler() override
    {
        return this;
    }

    void OnContextCreated(CefRefPtr<CefBrowser>, CefRefPtr<CefFrame> frame, CefRefPtr<CefV8Context> context) override
    {
        if (!frame || !context) {
            return;
        }

        const std::string userAgent = desktopUserAgent().toStdString();
        const std::string script = R"JS((function() {
    var userAgent = '__AEROCORE_USER_AGENT__';
    function constant(name, value) {
        try { Object.defineProperty(Navigator.prototype, name, { get: function() { return value; }, configurable: true }); } catch (e) {}
        try { Object.defineProperty(navigator, name, { get: function() { return value; }, configurable: true }); } catch (e) {}
    }
    constant('userAgent', userAgent);
    constant('platform', 'Win32');
    constant('appVersion', userAgent.replace(/^Mozilla\//, ''));
    constant('vendor', 'Google Inc.');
    constant('language', __AEROCORE_LANGUAGE__);
    constant('languages', __AEROCORE_LANGUAGES__);
    constant('maxTouchPoints', 0);
    constant('webdriver', false);
    constant('pdfViewerEnabled', true);
    constant('hardwareConcurrency', 8);
    constant('deviceMemory', 8);
    if (!window.chrome) {
        window.chrome = { runtime: {} };
    }
    try {
        Object.defineProperty(navigator, 'userAgentData', {
            configurable: true,
            get: function() {
                return {
                    brands: [
                        { brand: 'Chromium', version: '136' },
                        { brand: 'Google Chrome', version: '136' },
                        { brand: 'Not A(Brand', version: '24' }
                    ],
                    mobile: false,
                    platform: 'Windows',
                    getHighEntropyValues: function() {
                        return Promise.resolve({
                            architecture: 'x86',
                            bitness: '64',
                            brands: this.brands,
                            fullVersionList: [
                                { brand: 'Chromium', version: '136.0.0.0' },
                                { brand: 'Google Chrome', version: '136.0.0.0' },
                                { brand: 'Not A(Brand', version: '24.0.0.0' }
                            ],
                            mobile: false,
                            model: '',
                            platform: 'Windows',
                            platformVersion: '10.0.0',
                            uaFullVersion: '136.0.0.0',
                            wow64: false
                        });
                    }
                };
            }
        });
    } catch (e) {}
})();)JS";
        std::string patchedScript = script;
        const std::string placeholder = "__AEROCORE_USER_AGENT__";
        const size_t pos = patchedScript.find(placeholder);
        if (pos != std::string::npos) {
            patchedScript.replace(pos, placeholder.size(), userAgent);
        }
        const QJsonObject profile = distributionProfile();
        const QString language = profile.value(QStringLiteral("language"))
                                     .toString(QStringLiteral("zh-CN"));
        QJsonArray languages = profile.value(QStringLiteral("languages")).toArray();
        if (languages.isEmpty()) {
            languages = {language};
        }
        const auto replaceToken = [&patchedScript](const std::string &token, const std::string &value) {
            const size_t offset = patchedScript.find(token);
            if (offset != std::string::npos) {
                patchedScript.replace(offset, token.size(), value);
            }
        };
        const QByteArray encodedLanguage = QJsonDocument(QJsonArray{language}).toJson(QJsonDocument::Compact);
        replaceToken("__AEROCORE_LANGUAGE__", encodedLanguage.mid(1, encodedLanguage.size() - 2).toStdString());
        replaceToken("__AEROCORE_LANGUAGES__", QJsonDocument(languages).toJson(QJsonDocument::Compact).toStdString());
        if (frame->IsMain()) {
            CefRefPtr<CefV8Value> global = context->GetGlobal();
            global->SetValue("__aerocoreReportGpuDiagnostics",
                             CefV8Value::CreateFunction("aerocoreReportGpuDiagnostics", new GpuDiagnosticsReporter()),
                             V8_PROPERTY_ATTRIBUTE_NONE);
            patchedScript += "\n";
            patchedScript += gpuDiagnosticsScript().toStdString();
        }
        if (videoPlaybackTrackingEnabled() && frame->IsMain()) {
            CefRefPtr<CefV8Value> global = context->GetGlobal();
            global->SetValue("__aerocoreReportVideoPlayback",
                             CefV8Value::CreateFunction("aerocoreReportVideoPlayback", new MediaPlaybackReporter()),
                             V8_PROPERTY_ATTRIBUTE_NONE);
            patchedScript += "\n";
            patchedScript += videoPlaybackTrackingScript().toStdString();
        }
        frame->ExecuteJavaScript(patchedScript, frame->GetURL(), 0);
    }

private:
    IMPLEMENT_REFCOUNTING(AeroCoreCefApp);
};
}

int main(int argc, char *argv[])
{
#if defined(_WIN32)
    CefMainArgs cefMainArgs(GetModuleHandle(nullptr));
#else
    CefMainArgs cefMainArgs(argc, argv);
#endif
    CefRefPtr<AeroCoreCefApp> cefApp = new AeroCoreCefApp();
    const int subprocessExitCode = CefExecuteProcess(cefMainArgs, cefApp, nullptr);
    if (subprocessExitCode >= 0) {
        return subprocessExitCode;
    }

    QCoreApplication::setOrganizationName(QStringLiteral("AeroCore"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("aerocore.local"));
    QCoreApplication::setApplicationName(QStringLiteral("AeroCore Browser"));
    QCoreApplication::setApplicationVersion(QStringLiteral(AEROCORE_BROWSER_VERSION));

    AppLogger::init();

    QApplication app(argc, argv);
    QTranslator appTranslator;
    const QDir translationsDir(QFileInfo(QCoreApplication::applicationFilePath()).absoluteDir()
                                   .filePath(QStringLiteral("translations")));
    const QString systemLocale = QLocale::system().name();
    const QJsonObject profile = distributionProfile();
    QString requestedLocale = profile.value(QStringLiteral("language")).toString()
                                  .replace(QLatin1Char('-'), QLatin1Char('_'));
    if (requestedLocale.isEmpty()) {
        const QString variant = profile.value(QStringLiteral("variant")).toString();
        requestedLocale = variant == QStringLiteral("zh-CN")
            ? QStringLiteral("zh_CN")
            : systemLocale;
    }
    if (requestedLocale.isEmpty()) {
        requestedLocale = systemLocale;
    }
    const QString baseLocale = requestedLocale.section(QLatin1Char('_'), 0, 0);
    const QStringList localeCandidates = {requestedLocale, baseLocale};
    bool translationLoaded = false;
    for (const QString &locale : localeCandidates) {
        if (locale.isEmpty() || !appTranslator.load(QStringLiteral("aerocore-browser_%1").arg(locale),
                                                     translationsDir.absolutePath())) {
            continue;
        }
        translationLoaded = true;
        break;
    }
    if (translationLoaded) {
        app.installTranslator(&appTranslator);
    }
    qInfo().noquote() << QStringLiteral("UI localization: requested=%1 system=%2 translator_loaded=%3")
                             .arg(requestedLocale,
                                  systemLocale,
                                  translationLoaded ? QStringLiteral("yes") : QStringLiteral("no"));
#if defined(__linux__)
    installTerminationHandlers();
    installCrashBacktraceHandlers();

    // Steam OSK on gamescope: filter XI2 touch/gesture (QTBUG-114334) and XKB
    // MappingNotify / MapNotify / NewKeyboardNotify that crash in updateKeymap().
    XcbXi2CrashGuard xcbXi2CrashGuard;
    if (XcbXi2CrashGuard::shouldInstall()) {
        qInfo().noquote() << QStringLiteral(
            "Installing XcbXi2CrashGuard (XI2 touch/gesture + XKB keymap update)");
        app.installNativeEventFilter(&xcbXi2CrashGuard);
    }

    // Always install on xcb: text-input sessions must bypass broken xkb_state after Steam OSK.
    // AEROCORE_SUPPRESS_XCB_KEYBOARD still controls non-text navigation key remapping.
    XcbKeyboardSuppressor xcbKeyboardSuppressor;
    if (QGuiApplication::platformName() == QStringLiteral("xcb")) {
        qInfo().noquote() << QStringLiteral(
            "Installing XcbKeyboardSuppressor (text-session xkb bypass%1)")
                                 .arg(suppressXcbKeyboardEnabled()
                                          ? QStringLiteral(", nav remap on")
                                          : QStringLiteral(""));
        app.installNativeEventFilter(&xcbKeyboardSuppressor);
    }
#endif
    app.setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    QObject::connect(&app, &QGuiApplication::primaryScreenChanged, &app, [](QScreen *) {
        logQtScaleState(QStringLiteral("primary-screen-changed"));
    });
    logQtScaleState(QStringLiteral("app-startup"));

    SiteConfigManager siteConfig;
    g_siteConfigForStartup = &siteConfig;

    const QStringList arguments = QCoreApplication::arguments();
    // const bool startWindowed = arguments.contains(QStringLiteral("--windowed"));
    QString initialUrl;
    for (int index = 1; index < arguments.size(); ++index) {
        if (!arguments.at(index).startsWith(QStringLiteral("--"))) {
            initialUrl = arguments.at(index);
            break;
        }
    }

    CefSettings settings;
    settings.no_sandbox = true;
    settings.multi_threaded_message_loop = false;
    settings.persist_session_cookies = true;
    settings.windowless_rendering_enabled = true;

    const QFileInfo executableInfo(QCoreApplication::applicationFilePath());
    const QDir executableDir = executableInfo.absoluteDir();
    CefString(&settings.browser_subprocess_path) = executableInfo.absoluteFilePath().toStdString();
    CefString(&settings.resources_dir_path) = executableDir.absolutePath().toStdString();
    CefString(&settings.locales_dir_path) = executableDir.filePath(QStringLiteral("locales")).toStdString();

    const QString appDataRoot = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    const QString cacheRoot = qEnvironmentVariable(
        "AEROCORE_CEF_PROFILE_DIR",
        QDir(appDataRoot).filePath(QStringLiteral("cef-profile")));
    QDir().mkpath(cacheRoot);
    CefString(&settings.cache_path) = cacheRoot.toStdString();
    CefString(&settings.root_cache_path) = cacheRoot.toStdString();
    CefString(&settings.log_file) = QDir(cacheRoot).filePath(QStringLiteral("cef.log")).toStdString();
    settings.log_severity = LOGSEVERITY_INFO;
    CefString(&settings.user_agent) = desktopUserAgent().toStdString();
    const QJsonArray configuredLanguages = distributionProfile().value(QStringLiteral("languages")).toArray();
    QStringList acceptLanguages;
    for (const QJsonValue &value : configuredLanguages) {
        if (value.isString()) acceptLanguages.append(value.toString());
    }
    CefString(&settings.accept_language_list) =
        (acceptLanguages.isEmpty() ? QStringLiteral("zh-CN,zh,en-US,en")
                                   : acceptLanguages.join(QLatin1Char(','))).toStdString();

    prepareWidevineCdmHintFile(cacheRoot, executableDir);

    qInfo().noquote() << QStringLiteral("CEF app data root: %1").arg(appDataRoot);
    qInfo().noquote() << QStringLiteral("CEF cache path: %1").arg(cacheRoot);
    qInfo().noquote() << QStringLiteral("CEF log file: %1").arg(QDir(cacheRoot).filePath(QStringLiteral("cef.log")));
    qInfo().noquote() << QStringLiteral("CEF GPU mode: %1").arg(gpuMode());

    if (!CefInitialize(cefMainArgs, settings, cefApp, nullptr)) {
        return -1;
    }

#if defined(__linux__)
    // CEF/Breakpad often replaces SIGSEGV handlers; reinstall ours after init.
    installCrashBacktraceHandlers();

    installCefSpinGuard();
#endif

    QTimer cefPump;
    QObject::connect(&cefPump, &QTimer::timeout, []() {
        CefDoMessageLoopWork();
    });
    cefPump.start(10);

    StallWatchdog::installIfEnabled(&app);

    int exitCode = 0;
    {
        qmlRegisterSingletonType(QUrl("qrc:/qt/qml/AeroCoreBrowser/src/content/qml/base/AppUtils.qml"), "AppUtils", 1, 0, "AppUtils");
        qmlRegisterUncreatableMetaObject(
            InputTypes::staticMetaObject,
            "InputTypes",
            1,
            0,
            "InputTypes",
            QStringLiteral("InputTypes is a namespace"));

        MainController* mainController = new MainController();
        UpdateController updateController;
        BrowserController browserController;
        SettingsController settingsController(&siteConfig,
                                            mainController->getInputManager(),
                                            mainController,
                                            &browserController);
        mainController->setSettingsController(&settingsController);
        QObject::connect(&app, &QCoreApplication::aboutToQuit, mainController, [mainController]() {
            mainController->persistSessionsNow();
        });
        MemoryImageProvider memoryImageProvider;
        memoryImageProvider.setMainController(mainController);

        QQmlApplicationEngine* engine = new QQmlApplicationEngine();
        engine->addImageProvider("memory", &memoryImageProvider);
        engine->rootContext()->setContextProperty("cppInputManager", new InputManagerAdapter(mainController->getInputManager()));
        engine->rootContext()->setContextProperty("cppMainController", mainController);
        engine->rootContext()->setContextProperty("cppUpdater", &updateController);
        const QString variant = profile.value(QStringLiteral("variant")).toString();
        engine->rootContext()->setContextProperty("distributionVariant",
                                                   variant.isEmpty()
                                                       ? (requestedLocale == QStringLiteral("zh_CN")
                                                              ? QStringLiteral("zh-CN")
                                                              : QStringLiteral("international"))
                                                       : variant);
        engine->rootContext()->setContextProperty("cppBrowserController", &browserController);
        engine->rootContext()->setContextProperty("cppSettings", &settingsController);
        mainController->setQmlEngine(engine);
        mainController->run();

        QObject::connect(
            engine,
            &QQmlApplicationEngine::objectCreationFailed,
            &app,
            []() { QCoreApplication::exit(-1); },
            Qt::QueuedConnection);
        engine->loadFromModule("AeroCoreBrowser", "MainWindow");
        // Never make Steam wait for a remote DRM download. The CDM is picked
        // up on the next launch, so prepare it only after the first UI has
        // had time to load and paint.
        QTimer::singleShot(1500, &app, [executableDir]() {
            startWidevinePreparationAfterUiLoaded(executableDir.absolutePath());
        });

#if defined(__linux__)
        QTimer terminationPoll;
        terminationPoll.setInterval(100);
        QObject::connect(&terminationPoll, &QTimer::timeout, &app, [&app]() {
            if (gTerminationSignal == 0) {
                return;
            }
            const int signalNumber = gTerminationSignal;
            gTerminationSignal = 0;
            qWarning().noquote() << QStringLiteral("[shutdown] termination signal received: %1")
                                        .arg(signalNumber);
            app.quit();
        });
        terminationPoll.start();
#endif

        exitCode = app.exec();
    }

    qInfo().noquote() << QStringLiteral("[shutdown] app.exec returned exitCode=%1").arg(QString::number(exitCode));
    cefPump.stop();
    CefShutdown();
    AppLogger::shutdown();
    return exitCode;
}
