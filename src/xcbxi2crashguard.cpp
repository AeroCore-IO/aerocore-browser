#if defined(__linux__)

#include "xcbxi2crashguard.h"

#include "cursorutils.h"

#include <QDebug>
#include <QGuiApplication>
#include <QString>

#include <csignal>
#include <cstdlib>
#include <cstring>

#include <execinfo.h>
#include <fcntl.h>
#include <unistd.h>
#include <xcb/xcb.h>
#include <xcb/xproto.h>

namespace {

constexpr uint8_t kXcbGeGeneric = 35; // XCB_GE_GENERIC
// Core MappingNotify → QXcbKeyboard::updateKeymap(mapping_notify)
constexpr uint8_t kXcbMappingNotify = XCB_MAPPING_NOTIFY;

// xcb-xkb event xkbType values (avoid hard dep on libxcb-xkb at link time)
constexpr uint8_t kXkbNewKeyboardNotify = 0; // XCB_XKB_NEW_KEYBOARD_NOTIFY
constexpr uint8_t kXkbMapNotify = 1;         // XCB_XKB_MAP_NOTIFY
// XCB_XKB_NKN_DETAIL_KEYCODES — only this bit triggers updateKeymap in Qt
constexpr uint16_t kXkbNknDetailKeycodes = 0x01;

// https://xcb.freedesktop.org/manual/group__XCB__Input__API.html
enum XcbInputOpcode : uint16_t {
    Hierarchy = 11,
    DeviceChanged = 13,
    TouchBegin = 18,
    TouchUpdate = 19,
    TouchEnd = 20,
    GesturePinchBegin = 27,
    GesturePinchUpdate = 28,
    GesturePinchEnd = 29,
    GestureSwipeBegin = 30,
    GestureSwipeUpdate = 31,
    GestureSwipeEnd = 32,
};

// Layout matches xcb_xkb_*_event_t (xcb-xkb); we only need xkbType / changed.
#pragma pack(push, 1)
struct XkbAnyEventView {
    uint8_t response_type;
    uint8_t xkbType;
    uint16_t sequence;
    uint32_t time;
    uint8_t deviceID;
};

struct XkbNewKeyboardNotifyView {
    uint8_t response_type;
    uint8_t xkbType;
    uint16_t sequence;
    uint32_t time;
    uint8_t deviceID;
    uint8_t oldDeviceID;
    uint8_t minKeyCode; // xcb_keycode_t
    uint8_t maxKeyCode;
    uint8_t oldMinKeyCode;
    uint8_t oldMaxKeyCode;
    uint8_t requestMajor;
    uint8_t requestMinor;
    uint16_t changed;
};
#pragma pack(pop)

bool isProblematicXi2Event(uint16_t eventType)
{
    switch (eventType) {
    case TouchBegin:
    case TouchUpdate:
    case TouchEnd:
    case GesturePinchBegin:
    case GesturePinchUpdate:
    case GesturePinchEnd:
    case GestureSwipeBegin:
    case GestureSwipeUpdate:
    case GestureSwipeEnd:
        return true;
    default:
        return false;
    }
}

const char *xi2EventName(uint16_t eventType)
{
    switch (eventType) {
    case Hierarchy:
        return "HIERARCHY";
    case DeviceChanged:
        return "DEVICE_CHANGED";
    case TouchBegin:
        return "TOUCH_BEGIN";
    case TouchUpdate:
        return "TOUCH_UPDATE";
    case TouchEnd:
        return "TOUCH_END";
    case GesturePinchBegin:
        return "GESTURE_PINCH_BEGIN";
    case GesturePinchUpdate:
        return "GESTURE_PINCH_UPDATE";
    case GesturePinchEnd:
        return "GESTURE_PINCH_END";
    case GestureSwipeBegin:
        return "GESTURE_SWIPE_BEGIN";
    case GestureSwipeUpdate:
        return "GESTURE_SWIPE_UPDATE";
    case GestureSwipeEnd:
        return "GESTURE_SWIPE_END";
    default:
        return "UNKNOWN";
    }
}

bool envFlagEnabled(const char *name, bool defaultValue)
{
    if (!qEnvironmentVariableIsSet(name)) {
        return defaultValue;
    }
    const QString value = qEnvironmentVariable(name).trimmed().toLower();
    if (value == QStringLiteral("0") || value == QStringLiteral("false")
        || value == QStringLiteral("no") || value == QStringLiteral("off")) {
        return false;
    }
    if (value == QStringLiteral("1") || value == QStringLiteral("true")
        || value == QStringLiteral("yes") || value == QStringLiteral("on")) {
        return true;
    }
    return defaultValue;
}

struct XcbExtensionInfo {
    int majorOpcode = -1;
    int firstEvent = -1;
};

XcbExtensionInfo queryExtension(const char *name)
{
    XcbExtensionInfo info;
    int screen = 0;
    xcb_connection_t *conn = xcb_connect(nullptr, &screen);
    if (!conn || xcb_connection_has_error(conn)) {
        if (conn) {
            xcb_disconnect(conn);
        }
        return info;
    }

    const xcb_query_extension_cookie_t cookie =
        xcb_query_extension(conn, static_cast<uint16_t>(std::strlen(name)), name);
    xcb_query_extension_reply_t *reply = xcb_query_extension_reply(conn, cookie, nullptr);
    if (reply) {
        if (reply->present) {
            info.majorOpcode = reply->major_opcode;
            info.firstEvent = reply->first_event;
        }
        std::free(reply);
    }
    xcb_disconnect(conn);
    return info;
}

bool textInputActiveFromApp()
{
    return qApp && qApp->property("aerocoreTextInputActive").toBool();
}

int gCrashLogFd = -1;

void writeCrashFd(const char *data, size_t len)
{
    if (len == 0) {
        return;
    }
    (void)!::write(STDERR_FILENO, data, len);
    if (gCrashLogFd >= 0) {
        (void)!::write(gCrashLogFd, data, len);
    }
}

void crashBacktraceHandler(int signalNumber)
{
    const char header[] = "\n[crash] fatal signal received, native backtrace:\n";
    writeCrashFd(header, sizeof(header) - 1);

    const char sigLabel[] = "[crash] signal=";
    writeCrashFd(sigLabel, sizeof(sigLabel) - 1);
    if (signalNumber >= 0 && signalNumber <= 9) {
        char digit = static_cast<char>('0' + signalNumber);
        writeCrashFd(&digit, 1);
    } else if (signalNumber >= 10 && signalNumber <= 99) {
        char two[2] = {
            static_cast<char>('0' + (signalNumber / 10)),
            static_cast<char>('0' + (signalNumber % 10)),
        };
        writeCrashFd(two, 2);
    }
    const char nl[] = "\n";
    writeCrashFd(nl, 1);

    void *frames[64];
    const int count = ::backtrace(frames, 64);
    if (count > 0) {
        // Also dump to crash fd if possible (backtrace_symbols_fd only takes one fd)
        ::backtrace_symbols_fd(frames, count, STDERR_FILENO);
        if (gCrashLogFd >= 0) {
            ::backtrace_symbols_fd(frames, count, gCrashLogFd);
        }
    }

    const char footer[] = "[crash] end of backtrace\n";
    writeCrashFd(footer, sizeof(footer) - 1);

    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
}

void installCrashHandlersWithSigaction()
{
    struct sigaction action;
    std::memset(&action, 0, sizeof(action));
    action.sa_handler = crashBacktraceHandler;
    sigemptyset(&action.sa_mask);
    // Don't use SA_RESETHAND — we re-raise after logging
    action.sa_flags = SA_RESTART;

    sigaction(SIGSEGV, &action, nullptr);
    sigaction(SIGABRT, &action, nullptr);
    sigaction(SIGBUS, &action, nullptr);
    sigaction(SIGFPE, &action, nullptr);
    sigaction(SIGILL, &action, nullptr);
}

} // namespace

XcbXi2CrashGuard::XcbXi2CrashGuard()
{
    // Eager: filter touch/gesture immediately. Steam OSK may hotplug without a
    // hierarchy event reaching our filter first (or before we observe it).
    m_xi2Active = envFlagEnabled("AEROCORE_XI2_CRASH_GUARD_EAGER", true);
    // Keymap rebuilds from Steam OSK are the confirmed SIGSEGV path on Bazzite.
    // Default ON; disable with AEROCORE_XKB_KEYMAP_GUARD=0 if layout hot-reload is needed.
    m_keymapFilterActive = envFlagEnabled("AEROCORE_XKB_KEYMAP_GUARD", true);

    const XcbExtensionInfo xinput = queryExtension("XInputExtension");
    m_xinputOpcode = xinput.majorOpcode;
    if (m_xinputOpcode < 0) {
        qWarning().noquote() << QStringLiteral(
            "XcbXi2CrashGuard: XInput extension unavailable; XI2 touch/gesture filter inactive");
    }

    const XcbExtensionInfo xkb = queryExtension("XKEYBOARD");
    m_xkbFirstEvent = xkb.firstEvent;
    if (m_xkbFirstEvent < 0) {
        qWarning().noquote() << QStringLiteral(
            "XcbXi2CrashGuard: XKEYBOARD extension unavailable; XKB MapNotify filter inactive "
            "(MappingNotify still filtered)");
    }

    qInfo().noquote() << QStringLiteral(
        "XcbXi2CrashGuard: xinputOpcode=%1 xkbFirstEvent=%2 xi2Eager=%3 keymapGuard=%4")
                             .arg(m_xinputOpcode)
                             .arg(m_xkbFirstEvent)
                             .arg(m_xi2Active ? QStringLiteral("true") : QStringLiteral("false"))
                             .arg(m_keymapFilterActive ? QStringLiteral("true")
                                                       : QStringLiteral("false"));
}

bool XcbXi2CrashGuard::isReady() const
{
    // MappingNotify is a core event — always filterable on xcb.
    return true;
}

void XcbXi2CrashGuard::setTextInputSessionActive(bool active)
{
    m_textInputSessionActive = active;
    if (active && !m_keymapFilterActive) {
        // Session open: always block keymap rebuilds even if env disabled eager guard.
        qInfo().noquote() << QStringLiteral(
            "XcbXi2CrashGuard: text-input session → forcing keymap update filter");
    }
}

bool XcbXi2CrashGuard::shouldInstall()
{
    if (QGuiApplication::platformName() != QStringLiteral("xcb")) {
        qInfo().noquote() << QStringLiteral("XcbXi2CrashGuard: skip (platform=%1)")
                                 .arg(QGuiApplication::platformName());
        return false;
    }
    // Default ON for all xcb: Bazzite/Steam often strip GAMESCOPE_* from the app env.
    // Override with AEROCORE_XI2_CRASH_GUARD=0 to disable.
    const bool enabled = envFlagEnabled("AEROCORE_XI2_CRASH_GUARD", true);
    qInfo().noquote() << QStringLiteral("XcbXi2CrashGuard: shouldInstall=%1")
                             .arg(enabled ? QStringLiteral("true") : QStringLiteral("false"));
    return enabled;
}

bool XcbXi2CrashGuard::nativeEventFilter(const QByteArray &eventType, void *message, qintptr *)
{
    if (!message) {
        return false;
    }
    if (eventType != QByteArrayLiteral("xcb_generic_event_t")) {
        return false;
    }

    const auto *generic = static_cast<const xcb_generic_event_t *>(message);
    const uint8_t responseType = generic->response_type & ~uint8_t(0x80);

    const bool filterKeymap = m_keymapFilterActive || m_textInputSessionActive
                              || textInputActiveFromApp();

    // Core MappingNotify → updateKeymap (qxcbconnection.cpp)
    if (filterKeymap && responseType == kXcbMappingNotify) {
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
        refreshX11KeymapSnapshot();
#endif
        qInfo().noquote() << QStringLiteral(
            "XcbXi2CrashGuard: ignoring XCB_MAPPING_NOTIFY (blocks QXcbKeyboard::updateKeymap)");
        return true;
    }

    // XKB extension events share first_event; xkbType selects Map/NewKeyboard/State
    if (filterKeymap && m_xkbFirstEvent >= 0
        && responseType == static_cast<uint8_t>(m_xkbFirstEvent)) {
        const auto *xkbAny = static_cast<const XkbAnyEventView *>(message);
        if (xkbAny->xkbType == kXkbMapNotify) {
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
            refreshX11KeymapSnapshot();
#endif
            qInfo().noquote() << QStringLiteral(
                "XcbXi2CrashGuard: ignoring XKB_MAP_NOTIFY (blocks QXcbKeyboard::updateKeymap)");
            return true;
        }
        if (xkbAny->xkbType == kXkbNewKeyboardNotify) {
            const auto *nkn = static_cast<const XkbNewKeyboardNotifyView *>(message);
            if (nkn->changed & kXkbNknDetailKeycodes) {
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
                refreshX11KeymapSnapshot();
#endif
                qInfo().noquote() << QStringLiteral(
                    "XcbXi2CrashGuard: ignoring XKB_NEW_KEYBOARD_NOTIFY keycodes "
                    "(blocks QXcbKeyboard::updateKeymap)");
                return true;
            }
        }
        // STATE_NOTIFY can also touch a damaged xkb_state after OSK hotplug.
        if (xkbAny->xkbType == 2 /* XCB_XKB_STATE_NOTIFY */) {
            qInfo().noquote() << QStringLiteral(
                "XcbXi2CrashGuard: ignoring XKB_STATE_NOTIFY (avoids xkb_state update after OSK)");
            return true;
        }
    }

    if (m_xinputOpcode < 0) {
        return false;
    }

    const auto *ge = static_cast<const xcb_ge_generic_event_t *>(message);
    if (responseType != kXcbGeGeneric) {
        return false;
    }
    if (ge->extension != static_cast<uint8_t>(m_xinputOpcode)) {
        return false;
    }

    if (ge->event_type == Hierarchy || ge->event_type == DeviceChanged) {
        if (!m_xi2Active) {
            m_xi2Active = true;
        }
        // OSK hotplug often coincides with keymap rebuild — keep keymap filter on.
        if (!m_keymapFilterActive) {
            m_keymapFilterActive = true;
            qWarning().noquote() << QStringLiteral(
                "XcbXi2CrashGuard: XI2 %1 — enabling keymap update filter")
                                     .arg(QString::fromLatin1(xi2EventName(ge->event_type)));
        } else {
            qWarning().noquote() << QStringLiteral(
                "XcbXi2CrashGuard: XI2 %1 — filtering touch/gesture (QTBUG-114334)")
                                     .arg(QString::fromLatin1(xi2EventName(ge->event_type)));
        }
        return false; // let hierarchy/device-changed through
    }

    if (m_xi2Active && isProblematicXi2Event(ge->event_type)) {
        qInfo().noquote() << QStringLiteral("XcbXi2CrashGuard: ignoring XI2 %1 (%2)")
                                 .arg(QString::fromLatin1(xi2EventName(ge->event_type)))
                                 .arg(ge->event_type);
        return true;
    }

    return false;
}

void installCrashBacktraceHandlers()
{
    if (!envFlagEnabled("AEROCORE_CRASH_BACKTRACE", true)) {
        return;
    }

    // Dedicated crash log survives even if launcher only tails recent stdout
    if (gCrashLogFd < 0) {
        const QByteArray path = qgetenv("AEROCORE_CRASH_LOG");
        const char *crashPath = path.isEmpty() ? "/tmp/aerocore-browser-crash.log" : path.constData();
        gCrashLogFd = ::open(crashPath, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0644);
        if (gCrashLogFd >= 0) {
            qInfo().noquote() << QStringLiteral("Crash backtrace also written to %1")
                                     .arg(QString::fromUtf8(crashPath));
        }
    }

    installCrashHandlersWithSigaction();
    qInfo().noquote() << QStringLiteral("Installed native crash backtrace handlers (sigaction)");
}

#endif // __linux__
