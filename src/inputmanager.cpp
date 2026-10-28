#include "inputmanager.h"

#include "browserview.h"
#include "cursorutils.h"
#include "gamepadtrigger.h"
#include "setting/settingsstore.h"

#include <QApplication>
#include <QByteArray>
#include <QCoreApplication>
#include <QCursor>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QEvent>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QProcessEnvironment>
#include <QScreen>
#include <QString>
#include <QTimer>
#include <QTouchEvent>
#include <QWheelEvent>
#include <QWidget>
#include <QWindow>

#include <algorithm>
#include <cmath>

#if defined(Q_OS_LINUX)
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/XTest.h>
#undef KeyPress
#undef KeyRelease
#undef FocusIn
#undef FocusOut
#endif

namespace {
constexpr int kPollIntervalMs = 16;
constexpr qint64 kBackLongPressMs = 650;
constexpr Sint16 kAxisDeadzone = 12000;
constexpr qreal kCursorSpeed = 1350.0;
constexpr qreal kPreciseCursorSpeed = 260.0;
constexpr qreal kTurboCursorSpeed = 2800.0;
constexpr qreal kWheelSpeed = 1050.0;
constexpr qreal kPreciseWheelSpeed = 360.0;
constexpr qreal kTurboWheelSpeed = 2600.0;
constexpr qreal kBrowserWheelMultiplier = 3.0;
constexpr qreal kReferenceScreenWidth = 1920.0;
constexpr qreal kReferenceScreenHeight = 1080.0;
constexpr qreal kMaxCursorScreenSpeedScale = 3.4;
constexpr qreal kMaxWheelScreenSpeedScale = 2.4;
constexpr qreal kEdgeScrollMargin = 40.0;
constexpr qreal kEdgeScrollMaxMultiplier = 1.35;
constexpr qreal kTriggerMotionThreshold = 0.30;
constexpr qint64 kTriggerShortPressMaxMs = 360;
constexpr qint64 kShoulderShortPressMaxMs = 360;
constexpr qreal kFocusThreshold = 0.55;
constexpr qint64 kFocusInitialRepeatMs = 180;
constexpr qint64 kFocusRepeatMs = 110;
constexpr qint64 kDpadInitialRepeatMs = 280;
constexpr qint64 kDpadRepeatMs = 95;

qreal normalizeAxis(Sint16 value)
{
    const int magnitudeValue = value < 0 ? -(int(value)) : int(value);
    if (magnitudeValue <= kAxisDeadzone) {
        return 0.0;
    }

    const qreal sign = value < 0 ? -1.0 : 1.0;
    const qreal magnitude = (magnitudeValue - kAxisDeadzone) / qreal(SDL_JOYSTICK_AXIS_MAX - kAxisDeadzone);
    return sign * qBound(0.0, magnitude, 1.0);
}

bool applicationAcceptsControllerInput()
{
    return QGuiApplication::applicationState() == Qt::ApplicationActive;
}

bool windowHasInputFocus()
{
    return QApplication::activeWindow() || QGuiApplication::focusWindow();
}

QWindow *targetWindow()
{
    if (QWindow *window = QGuiApplication::focusWindow()) {
        return window;
    }

    const auto windows = QGuiApplication::topLevelWindows();
    return windows.isEmpty() ? nullptr : windows.constFirst();
}

QScreen *screenForGlobalPoint(const QPoint &globalPos)
{
    if (QScreen *screen = QGuiApplication::screenAt(globalPos)) {
        return screen;
    }

    return QGuiApplication::primaryScreen();
}

qreal resolutionScaleForGlobalPoint(const QPoint &globalPos)
{
    QScreen *screen = screenForGlobalPoint(globalPos);
    if (!screen) {
        return 1.0;
    }

    const QRect bounds = screen->availableGeometry();
    if (bounds.isEmpty()) {
        return 1.0;
    }

    const qreal widthScale = bounds.width() / kReferenceScreenWidth;
    const qreal heightScale = bounds.height() / kReferenceScreenHeight;
    return qMax<qreal>(1.0, qMax(widthScale, heightScale));
}

qreal cursorScreenSpeedScaleForGlobalPoint(const QPoint &globalPos)
{
    const qreal resolutionScale = resolutionScaleForGlobalPoint(globalPos);
    const qreal extraScale = resolutionScale - 1.0;
    const qreal scaled = 1.0 + extraScale * 1.35 + extraScale * extraScale * 0.35;
    return qBound(1.0, scaled, kMaxCursorScreenSpeedScale);
}

qreal wheelScreenSpeedScaleForGlobalPoint(const QPoint &globalPos)
{
    const qreal resolutionScale = resolutionScaleForGlobalPoint(globalPos);
    const qreal extraScale = resolutionScale - 1.0;
    const qreal scaled = 1.0 + extraScale * 0.85 + extraScale * extraScale * 0.10;
    return qBound(1.0, scaled, kMaxWheelScreenSpeedScale);
}

QObject *keyTarget()
{
    if (QWidget *widget = QApplication::focusWidget()) {
        return widget;
    }

    if (QObject *focusObject = QGuiApplication::focusObject()) {
        return focusObject;
    }
    return targetWindow();
}


QString printableFromKeyEvent(const QKeyEvent *keyEvent)
{
    if (!keyEvent) {
        return {};
    }
    if (!keyEvent->text().isEmpty()) {
        // Accept full commit strings (CJK may be multi-codepoint / multi-char)
        bool anyPrintable = false;
        for (const QChar ch : keyEvent->text()) {
            if (ch.isPrint() && !ch.isNull()) {
                anyPrintable = true;
                break;
            }
        }
        if (anyPrintable) {
            return keyEvent->text();
        }
    }

    const int key = keyEvent->key();
    const bool shift = keyEvent->modifiers().testFlag(Qt::ShiftModifier);
    if (key >= Qt::Key_A && key <= Qt::Key_Z) {
        const QChar ch(QLatin1Char(char('a' + (key - Qt::Key_A))));
        return shift ? QString(ch.toUpper()) : QString(ch);
    }
    if (key >= Qt::Key_0 && key <= Qt::Key_9) {
        return QString(QLatin1Char(char('0' + (key - Qt::Key_0))));
    }

    switch (key) {
    case Qt::Key_Space:
        return QStringLiteral(" ");
    case Qt::Key_Minus:
        return shift ? QStringLiteral("_") : QStringLiteral("-");
    case Qt::Key_Equal:
        return shift ? QStringLiteral("+") : QStringLiteral("=");
    case Qt::Key_Period:
        return shift ? QStringLiteral(">") : QStringLiteral(".");
    case Qt::Key_Slash:
        return shift ? QStringLiteral("?") : QStringLiteral("/");
    case Qt::Key_Colon:
        return QStringLiteral(":");
    case Qt::Key_Semicolon:
        return shift ? QStringLiteral(":") : QStringLiteral(";");
    default:
        return {};
    }
}

QObject *textOwnerForTarget(QObject *field)
{
    if (!field) {
        return nullptr;
    }
    if (QObject *parent = field->parent()) {
        if (parent->property("text").isValid()) {
            return parent;
        }
    }
    return field;
}

void appendTextToTarget(QObject *field, const QString &chunk)
{
    if (!field || chunk.isEmpty()) {
        return;
    }

    if (auto *browserView = qobject_cast<BrowserView *>(field)) {
        browserView->commitOskText(chunk);
        qInfo().noquote() << QStringLiteral("InputManager: append text=%1 to web field").arg(chunk);
        return;
    }

    QObject *owner = textOwnerForTarget(field);
    if (!owner) {
        return;
    }
    const QString current = owner->property("text").toString() + chunk;
    const bool ok = owner->setProperty("text", current);
    qInfo().noquote() << QStringLiteral("InputManager: append text=%1 ok=%2 len=%3")
                             .arg(chunk, ok ? QStringLiteral("true") : QStringLiteral("false"),
                                  QString::number(current.size()));
}

void backspaceTarget(QObject *field)
{
    if (!field) {
        return;
    }

    if (auto *browserView = qobject_cast<BrowserView *>(field)) {
        browserView->sendNavigationKey(Qt::Key_Backspace);
        qInfo() << "InputManager: backspace -> web field";
        return;
    }

    QObject *owner = textOwnerForTarget(field);
    if (!owner) {
        return;
    }
    QString current = owner->property("text").toString();
    if (current.isEmpty()) {
        return;
    }
    current.chop(1);
    owner->setProperty("text", current);
    qInfo() << "InputManager: backspace -> text len" << current.size();
}


bool containsSessionHint(const QString &value, std::initializer_list<const char *> hints)
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

bool hasSteamKeyboardSessionHint()
{
    if (qEnvironmentVariableIsSet("SteamDeck")
        || qEnvironmentVariableIsSet("GAMESCOPE_WAYLAND_DISPLAY")
        || qEnvironmentVariableIsSet("GAMESCOPE_SESSION")) {
        return true;
    }

    const QStringList sessionValues{
        qEnvironmentVariable("XDG_CURRENT_DESKTOP"),
        qEnvironmentVariable("XDG_SESSION_DESKTOP"),
        qEnvironmentVariable("DESKTOP_SESSION"),
        qEnvironmentVariable("XDG_SESSION_TYPE"),
        qEnvironmentVariable("SteamDeck"),
        qEnvironmentVariable("XDG_SESSION_CLASS"),
        qEnvironmentVariable("GAMESCOPE_NESTED_REFRESH")
    };

    for (const QString &value : sessionValues) {
        if (containsSessionHint(value, {"steamdeck", "gamescope", "steamos", "bazzite"})) {
            return true;
        }
    }

    if (qEnvironmentVariableIsSet("GAMESCOPE_NESTED_REFRESH")) {
        return true;
    }

    return false;
}

QString steamPipePath()
{
    const QString home = QDir::homePath();
    const QStringList candidates{
        QDir(home).filePath(QStringLiteral(".steam/steam.pipe")),
        QDir(home).filePath(QStringLiteral(".steam/steam/steam.pipe")),
        QDir(home).filePath(QStringLiteral(".local/share/Steam/steam.pipe")),
    };
    for (const QString &path : candidates) {
        if (QFileInfo::exists(path)) {
            return path;
        }
    }
    return candidates.constFirst();
}

bool openSteamDeeplinkViaPipe(const QString &urlString)
{
#if defined(Q_OS_LINUX)
    const QString pipePath = steamPipePath();
    if (!QFileInfo::exists(pipePath)) {
        return false;
    }

    const QByteArray payload = QByteArrayLiteral("steam ") + urlString.toUtf8() + '\n';
    const int fd = ::open(pipePath.toUtf8().constData(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    if (fd < 0) {
        qWarning().noquote() << QStringLiteral("InputManager: cannot open steam.pipe %1 errno=%2")
                                    .arg(pipePath)
                                    .arg(errno);
        return false;
    }

    const ssize_t written = ::write(fd, payload.constData(), static_cast<size_t>(payload.size()));
    ::close(fd);

    if (written != payload.size()) {
        qWarning().noquote() << QStringLiteral("InputManager: short write to steam.pipe (%1/%2)")
                                    .arg(static_cast<qint64>(written))
                                    .arg(payload.size());
        return false;
    }

    qInfo().noquote() << QStringLiteral("InputManager: Steam deeplink via steam.pipe %1").arg(pipePath);
    return true;
#else
    Q_UNUSED(urlString);
    return false;
#endif
}

bool openSteamDeeplink(const char *url)
{
    const QString urlString = QString::fromUtf8(url);
    qInfo().noquote() << QStringLiteral("InputManager: opening Steam deeplink %1").arg(urlString);

    if (openSteamDeeplinkViaPipe(urlString)) {
        return true;
    }

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("LD_PRELOAD"));

    auto launch = [&](const QString &program) -> bool {
        QProcess process;
        process.setProgram(program);
        process.setArguments({urlString});
        process.setProcessEnvironment(env);
        process.setProcessChannelMode(QProcess::ForwardedChannels);
        return process.startDetached();
    };

    if (launch(QStringLiteral("steam"))) {
        qInfo() << "InputManager: Steam deeplink via steam process (fallback)";
        return true;
    }
    if (launch(QStringLiteral("xdg-open"))) {
        qInfo() << "InputManager: Steam deeplink via xdg-open (fallback)";
        return true;
    }

    qWarning().noquote() << QStringLiteral("InputManager: failed to launch Steam deeplink %1").arg(urlString);
    return false;
}

QWidget *mouseTargetWidget(const QPoint &globalPos)
{
    if (QWidget *widget = QApplication::widgetAt(globalPos)) {
        return widget;
    }

    if (QWidget *activeWindow = QApplication::activeWindow()) {
        return activeWindow;
    }

    return QApplication::focusWidget();
}

QWindow *windowForObject(QObject *object)
{
    if (QWidget *widget = qobject_cast<QWidget *>(object)) {
        return widget->windowHandle();
    }

    if (QWindow *window = qobject_cast<QWindow *>(object)) {
        return window;
    }

    return targetWindow();
}

bool focusWidgetWithinDialogLikeOverlay()
{
    QWidget *widget = QApplication::focusWidget();
    while (widget) {
        if (widget->property("dialogLikeOverlay").toBool()) {
            return true;
        }
        widget = widget->parentWidget();
    }

    return false;
}

void loadControllerMappings()
{
    const QString mappingPath = QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("gamecontrollerdb.txt"));
    if (!QFileInfo::exists(mappingPath)) {
        return;
    }

    if (SDL_GameControllerAddMappingsFromFile(mappingPath.toUtf8().constData()) < 0) {
        qWarning("Failed to load SDL controller mappings from %s: %s",
                 qPrintable(mappingPath),
                 SDL_GetError());
    }
}

QString formatVidPid(quint16 vendor, quint16 product)
{
    return QStringLiteral("%1:%2")
        .arg(vendor, 4, 16, QLatin1Char('0'))
        .arg(product, 4, 16, QLatin1Char('0'));
}

bool countsAsTakeoverButton(int button)
{
#ifdef SDL_CONTROLLER_BUTTON_TOUCHPAD
    if (button == SDL_CONTROLLER_BUTTON_TOUCHPAD) {
        return false;
    }
#endif
    return true;
}

InputManager::InputDeviceKind gamepadKindFromKindId(const QString &kindId)
{
    if (kindId == GamepadCatalog::kKindPlayStation) {
        return InputManager::InputDeviceKind::GamepadPlayStation;
    }
    if (kindId == GamepadCatalog::kKindNintendo) {
        return InputManager::InputDeviceKind::GamepadNintendo;
    }
    if (kindId == GamepadCatalog::kKindSteam) {
        return InputManager::InputDeviceKind::GamepadSteam;
    }
    if (kindId == GamepadCatalog::kKindXbox) {
        return InputManager::InputDeviceKind::GamepadXbox;
    }
    return InputManager::InputDeviceKind::GamepadGeneric;
}

InputManager::InputDeviceKind gamepadKindFromName(const QString &name)
{
    return gamepadKindFromKindId(GamepadCatalog::kindIdFromName(name));
}

bool joystickButtonPressedAny(SDL_Joystick *joystick, std::initializer_list<int> indices)
{
    if (!joystick) {
        return false;
    }

    const int buttonCount = SDL_JoystickNumButtons(joystick);
    for (const int index : indices) {
        if (index >= 0 && index < buttonCount && SDL_JoystickGetButton(joystick, index) != 0) {
            return true;
        }
    }

    return false;
}

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
Display *x11MouseDisplay()
{
    return sharedX11Display();
}

bool x11SystemMouseAvailable()
{
    static bool initialized = false;
    static bool available = false;
    if (!initialized) {
        initialized = true;
        Display *display = x11MouseDisplay();
        int eventBase = 0;
        int errorBase = 0;
        int major = 0;
        int minor = 0;
        available = display && XTestQueryExtension(display, &eventBase, &errorBase, &major, &minor);
    }
    return available;
}

bool x11XfixesAvailable()
{
    static bool initialized = false;
    static bool available = false;
    if (!initialized) {
        initialized = true;
        Display *display = x11MouseDisplay();
        int eventBase = 0;
        int errorBase = 0;
        available = display && XFixesQueryExtension(display, &eventBase, &errorBase);
    }
    return available;
}

bool setSystemCursorVisibleNative(bool visible)
{
    if (!x11SystemMouseAvailable()) {
        return false;
    }

    if (useCustomCursorRendering()) {
        visible = false;
    }

    Display *display = x11MouseDisplay();
    const Window rootWindow = DefaultRootWindow(display);
    Window window = rootWindow;
    if (QWindow *focusedWindow = targetWindow()) {
        window = static_cast<Window>(focusedWindow->winId());
    }

    static ::Cursor blankCursor = 0;
    if (blankCursor == 0) {
        char data[1] = {0};
        Pixmap pixmap = XCreateBitmapFromData(display, DefaultRootWindow(display), data, 1, 1);
        if (pixmap) {
            XColor color{};
            blankCursor = XCreatePixmapCursor(display, pixmap, pixmap, &color, &color, 0, 0);
            XFreePixmap(display, pixmap);
        }
    }

    if (visible) {
        XUndefineCursor(display, rootWindow);
        XUndefineCursor(display, window);
    } else if (blankCursor != 0) {
        XDefineCursor(display, rootWindow, blankCursor);
        XDefineCursor(display, window, blankCursor);
    } else {
        XUndefineCursor(display, rootWindow);
        XUndefineCursor(display, window);
    }
    if (x11XfixesAvailable()) {
        if (visible) {
            XFixesShowCursor(display, rootWindow);
        } else {
            XFixesHideCursor(display, rootWindow);
        }
    }
    XFlush(display);
    return true;
}

unsigned int x11ButtonForQt(Qt::MouseButton button)
{
    switch (button) {
    case Qt::LeftButton:
        return 1;
    case Qt::MiddleButton:
        return 2;
    case Qt::RightButton:
        return 3;
    default:
        return 0;
    }
}

bool sendSystemMouseMoveNative(const QPoint &globalPos)
{
    if (!x11SystemMouseAvailable()) {
        return false;
    }

    Display *display = x11MouseDisplay();
    const Window rootWindow = DefaultRootWindow(display);
    if (x11XfixesAvailable() && !useCustomCursorRendering()) {
        XFixesShowCursor(display, rootWindow);
    }
    XWarpPointer(display, None, rootWindow, 0, 0, 0, 0, globalPos.x(), globalPos.y());
    XTestFakeMotionEvent(display, DefaultScreen(display), globalPos.x(), globalPos.y(), CurrentTime);
    XFlush(display);
    return true;
}

bool wakeSystemCursorNative(const QPoint &globalPos)
{
    if (!sendSystemMouseMoveNative(globalPos)) {
        return false;
    }

    Display *display = x11MouseDisplay();
    XTestFakeButtonEvent(display, 1, False, CurrentTime);
    XFlush(display);
    return true;
}

bool sendSystemMouseButtonNative(const QPoint &globalPos, Qt::MouseButton button, bool pressed)
{
    const unsigned int nativeButton = x11ButtonForQt(button);
    if (nativeButton == 0 || !sendSystemMouseMoveNative(globalPos)) {
        return false;
    }

    Display *display = x11MouseDisplay();
    XTestFakeButtonEvent(display, nativeButton, pressed ? True : False, CurrentTime);
    XFlush(display);
    return true;
}

bool sendSystemMouseClickNative(const QPoint &globalPos, Qt::MouseButton button)
{
    return sendSystemMouseButtonNative(globalPos, button, true)
        && sendSystemMouseButtonNative(globalPos, button, false);
}

bool sendSystemMouseWheelNative(const QPoint &globalPos, const QPoint &angleDelta)
{
    if (!sendSystemMouseMoveNative(globalPos)) {
        return false;
    }

    Display *display = x11MouseDisplay();
    const auto sendWheelButton = [display](unsigned int button, int steps) {
        for (int index = 0; index < steps; ++index) {
            XTestFakeButtonEvent(display, button, True, CurrentTime);
            XTestFakeButtonEvent(display, button, False, CurrentTime);
        }
    };

    if (angleDelta.y() > 0) {
        sendWheelButton(4, angleDelta.y() / 120);
    } else if (angleDelta.y() < 0) {
        sendWheelButton(5, -angleDelta.y() / 120);
    }

    if (angleDelta.x() > 0) {
        sendWheelButton(7, angleDelta.x() / 120);
    } else if (angleDelta.x() < 0) {
        sendWheelButton(6, -angleDelta.x() / 120);
    }

    XFlush(display);
    return true;
}
#endif
}

InputManager::InputManager(QObject *parent)
    : QObject(parent)
{
    applySteamInputBypassHints();
    SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
    loadControllerMappings();
    loadGamepadCatalog();

    qApp->installEventFilter(this);
    installSteamKeyboardDismissWatchers();

    connect(&m_pollTimer, &QTimer::timeout, this, &InputManager::pollGamepad);
    m_pollTimer.start(kPollIntervalMs);
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        m_cursorPosition = screen->availableGeometry().center();
    }
    openMissingPads();
    dropSteamVirtualPadsIfRealPresent();
    assignExclusive(GamepadCatalog::chooseExclusiveInstanceId(
                        connectedCatalogRecords(),
                        m_exclusiveInstanceKey,
                        GamepadCatalog::ExclusiveFallbackPolicy::ExternalThenBuiltin),
                    true);
    updateConnectedGamepadInfo();
    logSdlJoystickList("ctor");
    logGamepadInventory("ctor");
}

InputManager::~InputManager()
{
    if (m_steamKeyboardOpen) {
        openSteamDeeplink("steam://close/keyboard");
    }
    m_textInputActive = false;
    m_steamKeyboardOpen = false;
    clearTextInputTarget();
    syncTextInputActiveProperty();
    qApp->removeEventFilter(this);
    closeAllPads();
    SDL_QuitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS);
}

QPointF InputManager::cursorPosition() const
{
    return m_cursorPosition;
}

QString InputManager::navigationMode() const
{
    return m_navigationMode;
}

bool InputManager::preciseMode() const
{
    return m_preciseMode;
}

bool InputManager::turboMode() const
{
    return m_turboMode;
}

InputManager::SelectionInputSource InputManager::selectionInputSource() const
{
    return m_selectionInputSource;
}

InputManager::InputRoute InputManager::inputRoute() const
{
    return m_inputRoute;
}

void InputManager::setInputRoute(InputRoute route)
{
    if (m_inputRoute == route) {
        return;
    }

    if (route == InputRoute::UiFocus) {
        if (m_webTextInputSession) {
            closeWebKeyboardSession();
        }
        setDragLockActive(false);
        m_selectionInputSource = SelectionInputSource::Dpad;
        m_focusRepeatAtMs.fill(0);
        m_cursorRemainder = QPointF();
        m_edgeScrollRemainder = QPointF();
        m_scrollRemainder = QPointF();
        setSystemCursorVisible(false);
    } else {
        m_selectionInputSource = SelectionInputSource::Cursor;
    }

    m_inputRoute = route;
    // Must run after m_inputRoute is updated: initializeCursorAtScreenCenter()
    // early-returns unless isBrowserPointerRoute().
    if (route == InputRoute::BrowserPointer) {
        initializeCursorAtScreenCenter();
        if (useCustomCursorRendering()) {
            emit cursorSyncRequested();
        }
    }
    consumeInFlightPageActionEdges();
    m_suppressPageScopeInputUntilNextPoll = true;
    emit inputRouteChanged();
}

void InputManager::consumeInFlightPageActionEdges()
{
    m_backLongPressHandled = true;
    m_backPressedAtMs = 0;

    m_leftShoulderChordUsed = true;
    m_rightShoulderChordUsed = true;
    m_leftShoulderPressedAtMs = 0;
    m_rightShoulderPressedAtMs = 0;

    m_leftTriggerUsedForMotion = true;
    m_rightTriggerUsedForMotion = true;
    m_leftTriggerPressedAtMs = 0;
    m_rightTriggerPressedAtMs = 0;
}

bool InputManager::modalOverlayActive() const
{
    return m_modalOverlayActive;
}

void InputManager::setModalOverlayActive(bool active)
{
    if (m_modalOverlayActive == active) {
        return;
    }

    m_modalOverlayActive = active;
    emit modalOverlayActiveChanged();
}

InputManager::InputDeviceKind InputManager::activeInputDevice() const
{
    return m_activeInputDevice;
}

InputManager::InputDeviceKind InputManager::connectedGamepadKind() const
{
    return m_connectedGamepadKind;
}

QString InputManager::inputDeviceName() const
{
    return m_inputDeviceName;
}

QString InputManager::resolvedButtonIconLayout() const
{
    return m_resolvedButtonIconLayout;
}

QString InputManager::inputDeviceKindIdFor(InputDeviceKind kind)
{
    switch (kind) {
    case InputDeviceKind::Mouse:
        return QStringLiteral("mouse");
    case InputDeviceKind::Keyboard:
        return QStringLiteral("keyboard");
    case InputDeviceKind::TouchScreen:
        return QStringLiteral("touch");
    case InputDeviceKind::GamepadGeneric:
        return QStringLiteral("gamepad");
    case InputDeviceKind::GamepadXbox:
        return QStringLiteral("xbox");
    case InputDeviceKind::GamepadPlayStation:
        return QStringLiteral("playstation");
    case InputDeviceKind::GamepadNintendo:
        return QStringLiteral("nintendo");
    case InputDeviceKind::GamepadSteam:
        return QStringLiteral("steam");
    case InputDeviceKind::Unknown:
    default:
        return QStringLiteral("none");
    }
}

InputManager::InputDeviceKind InputManager::gamepadKindFromDeviceName(const QString &name)
{
    return gamepadKindFromName(name);
}

QString InputManager::gamepadButtonName(GamepadButton button)
{
    switch (button) {
    case GamepadButton::A:
        return QStringLiteral("A");
    case GamepadButton::B:
        return QStringLiteral("B");
    case GamepadButton::X:
        return QStringLiteral("X");
    case GamepadButton::Y:
        return QStringLiteral("Y");
    case GamepadButton::Back:
        return QStringLiteral("Back");
    case GamepadButton::Start:
        return QStringLiteral("Start");
    case GamepadButton::LB:
        return QStringLiteral("LB");
    case GamepadButton::RB:
        return QStringLiteral("RB");
    case GamepadButton::LT:
        return QStringLiteral("LT");
    case GamepadButton::RT:
        return QStringLiteral("RT");
    case GamepadButton::L3:
        return QStringLiteral("L3");
    case GamepadButton::R3:
        return QStringLiteral("R3");
    case GamepadButton::DpadUp:
        return QStringLiteral("DpadUp");
    case GamepadButton::DpadDown:
        return QStringLiteral("DpadDown");
    case GamepadButton::DpadLeft:
        return QStringLiteral("DpadLeft");
    case GamepadButton::DpadRight:
        return QStringLiteral("DpadRight");
    case GamepadButton::Guide:
        return QStringLiteral("Guide");
    }
    return QString();
}

QString InputManager::activeInputDeviceKindId() const
{
    return inputDeviceKindIdFor(m_activeInputDevice);
}

QString InputManager::connectedGamepadKindId() const
{
    return inputDeviceKindIdFor(m_connectedGamepadKind);
}

QString InputManager::activeInputDeviceDisplayName() const
{
    switch (m_activeInputDevice) {
    case InputDeviceKind::Mouse:
        return QStringLiteral("鼠标");
    case InputDeviceKind::Keyboard:
        return QStringLiteral("键盘");
    case InputDeviceKind::TouchScreen:
        return QStringLiteral("触摸屏");
    case InputDeviceKind::GamepadXbox:
        return QStringLiteral("Xbox 手柄");
    case InputDeviceKind::GamepadPlayStation:
        return QStringLiteral("PS 手柄");
    case InputDeviceKind::GamepadNintendo:
        return QStringLiteral("Switch 手柄");
    case InputDeviceKind::GamepadSteam:
        return QStringLiteral("Steam 手柄");
    case InputDeviceKind::GamepadGeneric:
        return QStringLiteral("手柄");
    case InputDeviceKind::Unknown:
    default:
        return QStringLiteral("未知");
    }
}

bool InputManager::isUiFocusRoute() const
{
    return m_inputRoute == InputRoute::UiFocus;
}

bool InputManager::isBrowserPointerRoute() const
{
    return m_inputRoute == InputRoute::BrowserPointer;
}

bool InputManager::shouldSuppressBrowserInjection() const
{
    return m_modalOverlayActive || focusWidgetWithinDialogLikeOverlay();
}

bool InputManager::routeNavigationThroughSignal() const
{
    return focusWidgetWithinDialogLikeOverlay();
}

void InputManager::dispatchNavigationKey(int key)
{
    if (routeNavigationThroughSignal()) {
        emit navigationKeyRequested(key);
        if (isBrowserPointerRoute()) {
            emit browserNavigationKeyRequested(key);
        }
        return;
    }

    sendKeyClick(key);
    emit navigationKeyRequested(key);
    if (isBrowserPointerRoute() && !m_modalOverlayActive) {
        emit browserNavigationKeyRequested(key);
    }
}

void InputManager::setActiveInputDevice(InputDeviceKind kind)
{
    if (m_activeInputDevice == kind) {
        return;
    }

    m_activeInputDevice = kind;
    emit activeInputDeviceChanged();
}

void InputManager::noteGamepadActivity()
{
    if (m_connectedGamepadKind != InputDeviceKind::Unknown) {
        setActiveInputDevice(m_connectedGamepadKind);
        return;
    }

    setActiveInputDevice(InputDeviceKind::GamepadGeneric);
}

InputManager::InputDeviceKind InputManager::kindFromKindId(const QString &kindId) const
{
    return gamepadKindFromKindId(kindId);
}

void InputManager::updateConnectedGamepadInfo()
{
    InputDeviceKind kind = InputDeviceKind::Unknown;
    QString name;
    QString detectedKindId;
    QString layoutOverride;

    const int exclusiveIndex = padIndexByInstanceKey(m_exclusiveInstanceKey);
    if (exclusiveIndex >= 0) {
        const OpenedPad &pad = m_pads[static_cast<size_t>(exclusiveIndex)];
        name = pad.name;
        kind = pad.detectedKind;
        detectedKindId = InputManager::inputDeviceKindIdFor(kind);
        const int recordIndex = GamepadCatalog::findRecordIndex(m_knownGamepads, pad.instanceKey);
        if (recordIndex >= 0) {
            detectedKindId = m_knownGamepads[recordIndex].detectedKindId;
            layoutOverride = m_knownGamepads[recordIndex].layoutOverride;
            kind = kindFromKindId(detectedKindId);
        }
    }

    const bool kindChanged = m_connectedGamepadKind != kind;
    const bool nameChanged = m_inputDeviceName != name;
    m_connectedGamepadKind = kind;
    m_inputDeviceName = name;

    QString layout = m_resolvedButtonIconLayout;
    if (exclusiveIndex >= 0) {
        layout = GamepadCatalog::resolveLayoutId(detectedKindId, layoutOverride);
    }

    const bool layoutChanged = m_resolvedButtonIconLayout != layout;
    m_resolvedButtonIconLayout = layout;

    if (kindChanged) {
        emit connectedGamepadChanged();
    }
    if (nameChanged) {
        emit inputDeviceNameChanged();
    }
    if (layoutChanged) {
        emit resolvedButtonIconLayoutChanged();
        persistGamepadCatalog();
    }

    if (kindChanged || nameChanged || layoutChanged) {
        qInfo().noquote() << QStringLiteral(
            "InputManager: exclusive presentation name=%1 kind=%2 layout=%3 override=%4 key=%5")
                              .arg(name.isEmpty() ? QStringLiteral("<none>") : name,
                                   inputDeviceKindIdFor(kind),
                                   m_resolvedButtonIconLayout,
                                   layoutOverride.isEmpty() ? QStringLiteral("auto") : layoutOverride,
                                   m_exclusiveInstanceKey.isEmpty() ? QStringLiteral("<none>")
                                                                    : m_exclusiveInstanceKey);
    }

    if (kind != InputDeviceKind::Unknown) {
        setActiveInputDevice(kind);
    }
}

void InputManager::initializeCursorAtScreenCenter()
{
    if (!isBrowserPointerRoute()) {
        return;
    }

    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QPoint center = screen->availableGeometry().center();
        QCursor::setPos(center);
        sendMouseMove(center);
    }
}

bool InputManager::homeNavigationEnabled() const
{
    return isUiFocusRoute();
}

bool InputManager::dragLockActive() const
{
    return m_dragLockActive;
}

Qt::MouseButtons InputManager::heldMouseButtons() const
{
    return m_heldMouseButtons;
}

bool InputManager::eventFilter(QObject *watched, QEvent *event)
{
    if (redirectTextInputEvent(watched, event)) {
        return true;
    }

    switch (event->type()) {
    case QEvent::MouseMove:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease: {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        setActiveInputDevice(InputDeviceKind::Mouse);
        if (isBrowserPointerRoute()) {
            updateCursorPosition(mouseEvent->globalPosition());
        }
        break;
    }
    case QEvent::TouchBegin:
    case QEvent::TouchUpdate:
    case QEvent::TouchEnd:
    case QEvent::TouchCancel: {
        auto *touchEvent = static_cast<QTouchEvent *>(event);
        if (!touchEvent->pointingDevice() || touchEvent->pointingDevice()->type() != QInputDevice::DeviceType::TouchScreen) {
            break;
        }

        QWindow *window = windowForObject(watched);
        if (!window) {
            break;
        }

        setActiveInputDevice(InputDeviceKind::TouchScreen);

        QPointF globalPos = m_lastTouchPosition;
        if (!touchEvent->points().isEmpty()) {
            const QEventPoint &point = touchEvent->points().constFirst();
            globalPos = point.globalPosition();
            m_lastTouchPosition = globalPos;
        } else if (event->type() == QEvent::TouchBegin || event->type() == QEvent::TouchUpdate) {
            break;
        }

        if (isBrowserPointerRoute()) {
            updateCursorPosition(globalPos);
        }

        if (event->type() == QEvent::TouchBegin) {
            m_touchMousePressed = true;
            if (isBrowserPointerRoute() && m_activeBrowserView) {
                sendTouchToActiveBrowser(QEvent::MouseButtonPress, globalPos);
            } else if (QWidget *widget = mouseTargetWidget(globalPos.toPoint())) {
                const QPointF localPos = QPointF(widget->mapFromGlobal(globalPos.toPoint()));
                const QPointF scenePos = QPointF(widget->window()->mapFromGlobal(globalPos.toPoint()));
                QMouseEvent mouseEvent(
                    QEvent::MouseButtonPress,
                    localPos,
                    scenePos,
                    globalPos,
                    Qt::LeftButton,
                    Qt::LeftButton,
                    Qt::NoModifier,
                    QPointingDevice::primaryPointingDevice());
                QCoreApplication::sendEvent(widget, &mouseEvent);
            } else {
                sendTouchMouseEvent(window, QEvent::MouseButtonPress, globalPos, Qt::LeftButton, Qt::LeftButton);
            }
        } else if (event->type() == QEvent::TouchUpdate) {
            if (!m_touchMousePressed) {
                break;
            }
            if (isBrowserPointerRoute() && m_activeBrowserView) {
                sendTouchToActiveBrowser(QEvent::MouseMove, globalPos);
            } else if (QWidget *widget = mouseTargetWidget(globalPos.toPoint())) {
                const QPointF localPos = QPointF(widget->mapFromGlobal(globalPos.toPoint()));
                const QPointF scenePos = QPointF(widget->window()->mapFromGlobal(globalPos.toPoint()));
                QMouseEvent mouseEvent(
                    QEvent::MouseMove,
                    localPos,
                    scenePos,
                    globalPos,
                    Qt::NoButton,
                    Qt::LeftButton,
                    Qt::NoModifier,
                    QPointingDevice::primaryPointingDevice());
                QCoreApplication::sendEvent(widget, &mouseEvent);
            } else {
                sendTouchMouseEvent(window, QEvent::MouseMove, globalPos, Qt::NoButton, Qt::LeftButton);
            }
        } else {
            if (!m_touchMousePressed) {
                break;
            }
            m_touchMousePressed = false;
            if (isBrowserPointerRoute() && m_activeBrowserView) {
                sendTouchToActiveBrowser(QEvent::MouseButtonRelease, globalPos);
            } else if (QWidget *widget = mouseTargetWidget(globalPos.toPoint())) {
                const QPointF localPos = QPointF(widget->mapFromGlobal(globalPos.toPoint()));
                const QPointF scenePos = QPointF(widget->window()->mapFromGlobal(globalPos.toPoint()));
                QMouseEvent mouseEvent(
                    QEvent::MouseButtonRelease,
                    localPos,
                    scenePos,
                    globalPos,
                    Qt::LeftButton,
                    Qt::NoButton,
                    Qt::NoModifier,
                    QPointingDevice::primaryPointingDevice());
                QCoreApplication::sendEvent(widget, &mouseEvent);
            } else {
                sendTouchMouseEvent(window, QEvent::MouseButtonRelease, globalPos, Qt::LeftButton, Qt::NoButton);
            }
        }

        touchEvent->accept();
        return true;
    }
    default:
        break;
    }

    return QObject::eventFilter(watched, event);
}

void InputManager::triggerAction(const QString &action)
{
    if (action == QStringLiteral("toggleNavigationMode")) {
        return;
    }
    if (!isBrowserPointerRoute()) {
        return;
    }
    emit actionTriggered(action);
}

void InputManager::dispatchPageAction(const QString &action)
{
    if (m_suppressPageScopeInputUntilNextPoll || !isBrowserPointerRoute()) {
        return;
    }
    triggerAction(action);
}

void InputManager::emitGamepadButtonEdge(GamepadButton button, bool pressed)
{
    const int index = static_cast<int>(button);
    if (index < 0 || index >= GamepadButtonCount) {
        return;
    }

    const std::size_t stateIndex = static_cast<std::size_t>(index);
    const bool wasPressed = m_gamepadButtonStates[stateIndex];
    if (pressed && !wasPressed) {
        emit gamepadButtonPressed(gamepadButtonName(button));
    } else if (!pressed && wasPressed) {
        emit gamepadButtonReleased(gamepadButtonName(button));
    }
    m_gamepadButtonStates[stateIndex] = pressed;
}

void InputManager::updateGamepadButtonEdges(bool emitSignals,
                                            bool actionPressed,
                                            bool backPressed,
                                            bool leftShoulderPressed,
                                            bool rightShoulderPressed,
                                            bool leftTriggerPressed,
                                            bool rightTriggerPressed)
{
    const auto update = [this, emitSignals](GamepadButton button, bool pressed) {
        if (emitSignals) {
            emitGamepadButtonEdge(button, pressed);
            return;
        }

        const int index = static_cast<int>(button);
        if (index >= 0 && index < GamepadButtonCount) {
            m_gamepadButtonStates[static_cast<std::size_t>(index)] = pressed;
        }
    };

    update(GamepadButton::A, actionPressed);
    update(GamepadButton::B, backPressed);
    update(GamepadButton::LB, leftShoulderPressed);
    update(GamepadButton::RB, rightShoulderPressed);
    update(GamepadButton::LT, leftTriggerPressed);
    update(GamepadButton::RT, rightTriggerPressed);
    update(GamepadButton::X, buttonPressed(SDL_CONTROLLER_BUTTON_X));
    update(GamepadButton::Y, buttonPressed(SDL_CONTROLLER_BUTTON_Y));
    update(GamepadButton::Back, buttonPressed(SDL_CONTROLLER_BUTTON_BACK));
    update(GamepadButton::Start, buttonPressed(SDL_CONTROLLER_BUTTON_START));
    update(GamepadButton::L3, buttonPressed(SDL_CONTROLLER_BUTTON_LEFTSTICK));
    update(GamepadButton::R3, buttonPressed(SDL_CONTROLLER_BUTTON_RIGHTSTICK));
    update(GamepadButton::Guide, buttonPressed(SDL_CONTROLLER_BUTTON_GUIDE));
    update(GamepadButton::DpadUp, buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_UP));
    update(GamepadButton::DpadDown, buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    update(GamepadButton::DpadLeft, buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
    update(GamepadButton::DpadRight, buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));
}

void InputManager::toggleNavigationMode()
{
}

void InputManager::setPreciseMode(bool enabled)
{
    if (m_preciseMode == enabled) {
        return;
    }
    m_preciseMode = enabled;
    emit preciseModeChanged();
}

void InputManager::setTurboMode(bool enabled)
{
    if (m_turboMode == enabled) {
        return;
    }
    m_turboMode = enabled;
    emit turboModeChanged();
}

qreal InputManager::cursorSpeedScale() const
{
    return m_cursorSpeedScale;
}

qreal InputManager::wheelSpeedScale() const
{
    return m_wheelSpeedScale;
}

bool InputManager::edgeScrollEnabled() const
{
    return m_edgeScrollEnabled;
}

void InputManager::setCursorSpeedScale(qreal scale)
{
    const qreal bounded = qBound(0.5, scale, 2.0);
    if (qFuzzyCompare(m_cursorSpeedScale, bounded)) {
        return;
    }
    m_cursorSpeedScale = bounded;
    emit cursorSpeedScaleChanged();
}

void InputManager::setWheelSpeedScale(qreal scale)
{
    const qreal bounded = qBound(0.5, scale, 2.0);
    if (qFuzzyCompare(m_wheelSpeedScale, bounded)) {
        return;
    }
    m_wheelSpeedScale = bounded;
    emit wheelSpeedScaleChanged();
}

void InputManager::setEdgeScrollEnabled(bool enabled)
{
    if (m_edgeScrollEnabled == enabled) {
        return;
    }
    m_edgeScrollEnabled = enabled;
    m_edgeScrollRemainder = QPointF();
    emit edgeScrollEnabledChanged();
}

void InputManager::setHomeNavigationEnabled(bool enabled)
{
    setInputRoute(enabled ? InputRoute::UiFocus : InputRoute::BrowserPointer);
}

void InputManager::consumeBackButton()
{
    if (m_buttonStates[SDL_CONTROLLER_BUTTON_B]) {
        m_backConsumed = true;
    } else {
        m_backConsumePending = true;
    }
}

void InputManager::nudgeCursorByKeyboard(const QPoint &delta)
{
    if (delta.isNull()) {
        return;
    }

    setActiveInputDevice(InputDeviceKind::Keyboard);
    if (!isBrowserPointerRoute()) {
        return;
    }

    nudgeCursor(delta);
}

void InputManager::setBrowserFullscreenActive(bool active)
{
    if (m_browserFullscreenActive == active) {
        return;
    }

    m_browserFullscreenActive = active;
    if (!active) {
        setDragLockActive(false);
        m_edgeScrollRemainder = QPointF();
        m_scrollRemainder = QPointF();
    }
}

void InputManager::setActiveBrowserView(BrowserView *view)
{
    m_activeBrowserView = view;
}

BrowserView *InputManager::activeBrowserView() const
{
    return m_activeBrowserView.data();
}

void InputManager::setSystemCursorVisible(bool visible)
{
    if (useCustomCursorRendering()) {
        visible = false;
    }

    if (m_systemCursorVisible == visible) {
        return;
    }

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    if (setSystemCursorVisibleNative(visible)) {
        m_systemCursorVisible = visible;
        return;
    }
#endif

    m_systemCursorVisible = visible;
}

void InputManager::refreshSystemCursor()
{
    if (isUiFocusRoute()) {
        return;
    }

    if (useCustomCursorRendering()) {
        setSystemCursorVisible(false);
        emit cursorSyncRequested();
        emit browserCursorSyncRequested();
        return;
    }

    setSystemCursorVisible(true);

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    if (x11SystemMouseAvailable()) {
        const QPoint base = m_cursorPosition.toPoint();
        QPoint wiggle = base;

        if (QScreen *screen = QGuiApplication::screenAt(base) ? QGuiApplication::screenAt(base) : QGuiApplication::primaryScreen()) {
            const QRect bounds = screen->availableGeometry();
            const int dx = base.x() < bounds.right() ? 1 : (base.x() > bounds.left() ? -1 : 0);
            const int dy = (dx == 0 && base.y() < bounds.bottom()) ? 1 : 0;
            wiggle = QPoint(base.x() + dx, base.y() + dy);
        }

        if (wiggle != base && sendSystemMouseMoveNative(wiggle) && sendSystemMouseMoveNative(base)) {
            wakeSystemCursorNative(base);
            emit cursorSyncRequested();
            emit browserCursorSyncRequested();
            return;
        }

        wakeSystemCursorNative(base);
    }
#endif

    emit cursorSyncRequested();
    emit browserCursorSyncRequested();
}

bool InputManager::textInputActive() const
{
    return m_textInputActive;
}

void InputManager::syncTextInputActiveProperty()
{
    if (qApp) {
        qApp->setProperty("aerocoreTextInputActive", m_textInputActive);
        qApp->setProperty("aerocoreTextInputGraceUntilMs", m_textInputGraceUntilMs);
    }
}

void InputManager::captureTextInputTarget()
{
    QObject *target = QGuiApplication::focusObject();
    if (!target) {
        target = keyTarget();
    }
    m_textInputTarget = target;
    if (m_textInputTarget) {
        qInfo().noquote() << QStringLiteral("InputManager: text input target=%1")
                                 .arg(QString::fromLatin1(m_textInputTarget->metaObject()->className()));
    } else {
        qWarning() << "InputManager: no text input target when opening Steam keyboard";
    }
}

void InputManager::clearTextInputTarget()
{
    m_textInputTarget.clear();
}

void InputManager::noteTextInputActivity()
{
    m_lastTextInputAtMs = QDateTime::currentMSecsSinceEpoch();
}

bool InputManager::shouldAcceptAppendChunk(const QString &chunk, qint64 nowMs)
{
    if (chunk.isEmpty()) {
        return false;
    }

    static constexpr qint64 kAppendDedupeWindowMs = 40;
    if (chunk == m_lastAppendChunk && nowMs - m_lastAppendAtMs < kAppendDedupeWindowMs) {
        qInfo().noquote() << QStringLiteral("InputManager: dedupe append text=%1 (dt=%2ms)")
                                 .arg(chunk)
                                 .arg(nowMs - m_lastAppendAtMs);
        return false;
    }

    m_lastAppendChunk = chunk;
    m_lastAppendAtMs = nowMs;
    return true;
}

bool InputManager::redirectTextInputEvent(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    const bool inGrace = nowMs < m_textInputGraceUntilMs;
    if (!m_textInputActive && !inGrace) {
        return false;
    }

    switch (event->type()) {
    case QEvent::KeyPress:
    case QEvent::KeyRelease: {
        auto *keyEvent = static_cast<QKeyEvent *>(event);
        const int key = keyEvent->key();
        const bool isEnter = key == Qt::Key_Return || key == Qt::Key_Enter;
        const bool isEscape = key == Qt::Key_Escape;
        const bool isNavigationKey = key == Qt::Key_Up || key == Qt::Key_Down
            || key == Qt::Key_Left || key == Qt::Key_Right
            || key == Qt::Key_Tab || key == Qt::Key_Backtab;

        if (isEnter || isEscape) {
            if (event->type() == QEvent::KeyPress) {
                qInfo().noquote() << QStringLiteral("InputManager: swallow %1 during Steam keyboard session")
                                         .arg(isEnter ? QStringLiteral("Enter") : QStringLiteral("Esc"));
                if (m_webTextInputSession) {
                    closeWebKeyboardSession();
                } else if (m_textInputActive || m_steamKeyboardOpen) {
                    acknowledgeSteamKeyboardClosed();
                }
            }
            return true;
        }

        if (isNavigationKey) {
            const bool isCaretKey = key == Qt::Key_Left || key == Qt::Key_Right;
            if (isCaretKey && event->type() == QEvent::KeyPress) {
                if (auto *browserView = qobject_cast<BrowserView *>(m_textInputTarget.data())) {
                    browserView->sendNavigationKey(key);
                    return true;
                }
                if (!m_webTextInputSession) {
                    return false;
                }
            }
            return oskBlockingSession();
        }

        if (!m_textInputActive || !m_textInputTarget) {
            return inGrace;
        }

        if (event->type() == QEvent::KeyRelease) {
            return true;
        }

        if (m_webTextInputSession && m_webKeyboardClosedAtMs > 0
            && nowMs - m_webKeyboardClosedAtMs < 500) {
            return true;
        }

        if (keyEvent->isAutoRepeat()) {
            return true;
        }

        if (key == Qt::Key_Backspace) {
            backspaceTarget(m_textInputTarget.data());
            m_lastAppendChunk.clear();
            m_lastAppendAtMs = 0;
            noteTextInputActivity();
            return true;
        }
        if (key == Qt::Key_Delete) {
            return true;
        }

        const QString chunk = printableFromKeyEvent(keyEvent);
        if (!chunk.isEmpty()) {
            if (shouldAcceptAppendChunk(chunk, nowMs)) {
                appendTextToTarget(m_textInputTarget.data(), chunk);
                noteTextInputActivity();
            }
            return true;
        }

        // Key_unknown with empty printable text: still log for CJK diagnosis
        static int s_ignoredKeyLogBudget = 12;
        if (s_ignoredKeyLogBudget > 0) {
            --s_ignoredKeyLogBudget;
            qInfo().noquote() << QStringLiteral("InputManager: ignored key=%1 text=%2")
                                     .arg(keyEvent->key())
                                     .arg(keyEvent->text());
        }
        return true;
    }
    case QEvent::InputMethod: {
        if (!m_textInputActive || !m_textInputTarget) {
            return false;
        }
        auto *imEvent = static_cast<QInputMethodEvent *>(event);
        if (auto *browserView = qobject_cast<BrowserView *>(m_textInputTarget.data())) {
            browserView->forwardInputMethodEvent(imEvent);
            noteTextInputActivity();
            return true;
        }
        if (!imEvent->commitString().isEmpty()) {
            const QString chunk = imEvent->commitString();
            qInfo().noquote() << QStringLiteral("InputManager: InputMethod commit=%1")
                                     .arg(chunk);
            if (shouldAcceptAppendChunk(chunk, nowMs)) {
                appendTextToTarget(m_textInputTarget.data(), chunk);
                noteTextInputActivity();
            }
            return true;
        }
        // Keep preedit from reaching CEF/other focus targets during OSK session
        return true;
    }
    default:
        return false;
    }
}

bool InputManager::isSteamKeyboardAvailable() const
{
    return hasSteamKeyboardSessionHint();
}

bool InputManager::oskBlockingSession() const
{
    if (!m_textInputActive) {
        return false;
    }
    return m_steamKeyboardOpen;
}

void InputManager::scheduleWindowReactivateAfterOsk()
{
    m_windowReactivateAttempts = 0;
    if (!m_windowReactivateTimer) {
        m_windowReactivateTimer = new QTimer(this);
        m_windowReactivateTimer->setInterval(500);
        connect(m_windowReactivateTimer, &QTimer::timeout, this, [this]() {
            if (m_steamKeyboardOpen) {
                m_windowReactivateTimer->stop();
                return;
            }
            if (windowHasInputFocus()) {
                m_windowReactivateTimer->stop();
                if (m_windowReactivateAttempts > 0) {
                    qInfo().noquote() << QStringLiteral("InputManager: window reactivated after OSK, attempts=%1")
                                             .arg(m_windowReactivateAttempts);
                }
                return;
            }
            if (++m_windowReactivateAttempts >= 8) {
                m_windowReactivateTimer->stop();
                qWarning().noquote() << QStringLiteral("InputManager: window reactivation after OSK gave up: appState=%1 focusWindow=%2 activeWindow=%3")
                                            .arg(QGuiApplication::applicationState())
                                            .arg(reinterpret_cast<quintptr>(QGuiApplication::focusWindow()))
                                            .arg(reinterpret_cast<quintptr>(QApplication::activeWindow()));
                return;
            }
            if (QWindow *w = targetWindow()) {
                w->raise();
                w->requestActivate();
            }
        });
    }
    m_windowReactivateTimer->start();
}

void InputManager::openSteamKeyboard()
{
    if (!isSteamKeyboardAvailable()) {
        qWarning() << "InputManager: Steam keyboard unavailable in this session";
        return;
    }

    if (m_textInputActive) {
        qInfo() << "InputManager: stale keyboard session detected on re-open, resetting";
        m_textInputActive = false;
        m_steamKeyboardOpen = false;
        m_steamKeyboardSawInactive = false;
        m_webTextInputSession = false;
        m_webKeyboardClosedAtMs = 0;
        clearTextInputTarget();
        syncTextInputActiveProperty();
        emit textInputActiveChanged();
    }

    captureTextInputTarget();

    if (!m_textInputActive) {
        m_textInputActive = true;
        m_textInputBackWasPressed = false;
        syncTextInputActiveProperty();
        emit textInputActiveChanged();
    }

    if (m_steamKeyboardOpen) {
        qInfo() << "InputManager: Steam keyboard already open";
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        if (!m_textInputActive || m_steamKeyboardOpen) {
            return;
        }

        static constexpr char kOpenKeyboardUrl[] =
            "steam://open/keyboard?XPosition=0&YPosition=0&Width=0&Height=0&Mode=0";

        if (!openSteamDeeplink(kOpenKeyboardUrl)) {
            qWarning() << "InputManager: failed to open Steam keyboard";
            if (m_webTextInputSession) {
                qInfo() << "InputManager: web keyboard session retained for manual Steam+X";
                return;
            }
            m_textInputActive = false;
            clearTextInputTarget();
            syncTextInputActiveProperty();
            emit textInputActiveChanged();
            return;
        }

        m_steamKeyboardOpen = true;
        m_steamKeyboardOpenedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_lastTextInputAtMs = 0;
        m_lastAppendAtMs = 0;
        m_lastAppendChunk.clear();
        m_leftStickWasActive = false;
        m_steamKeyboardSawInactive = QGuiApplication::applicationState() != Qt::ApplicationActive;
        qInfo() << "InputManager: Steam keyboard open requested (Mode=0, self-dismiss OK)";
    });
}

void InputManager::openWebKeyboardSession(BrowserView *view)
{
    if (!view) {
        return;
    }

    m_webTextInputSession = true;
    m_webKeyboardClosedAtMs = 0;
    m_textInputTarget = view;
    if (!m_textInputActive) {
        m_textInputActive = true;
        m_textInputBackWasPressed = false;
        syncTextInputActiveProperty();
        emit textInputActiveChanged();
    }
    qInfo() << "InputManager: web keyboard session started (target=BrowserView)";

    if (m_steamKeyboardOpen) {
        qInfo() << "InputManager: Steam keyboard already open";
        return;
    }
    if (!isSteamKeyboardAvailable()) {
        qInfo() << "InputManager: Steam keyboard hint missing; waiting for manual Steam+X";
        return;
    }

    QTimer::singleShot(0, this, [this]() {
        if (!m_textInputActive || m_steamKeyboardOpen) {
            return;
        }

        static constexpr char kOpenKeyboardUrl[] =
            "steam://open/keyboard?XPosition=0&YPosition=0&Width=0&Height=0&Mode=0";

        if (!openSteamDeeplink(kOpenKeyboardUrl)) {
            qWarning() << "InputManager: failed to open Steam keyboard; waiting for manual Steam+X";
            return;
        }

        m_steamKeyboardOpen = true;
        m_steamKeyboardOpenedAtMs = QDateTime::currentMSecsSinceEpoch();
        m_lastTextInputAtMs = 0;
        m_lastAppendAtMs = 0;
        m_lastAppendChunk.clear();
        m_leftStickWasActive = false;
        m_steamKeyboardSawInactive = QGuiApplication::applicationState() != Qt::ApplicationActive;
        qInfo() << "InputManager: Steam keyboard open requested for web field (Mode=0, self-dismiss OK)";
    });
}

void InputManager::closeWebKeyboardSession()
{
    if (!m_webTextInputSession) {
        return;
    }

    const bool wasActive = m_textInputActive;
    const bool wasOpen = m_steamKeyboardOpen;

    m_webTextInputSession = false;
    m_webKeyboardClosedAtMs = 0;
    m_textInputActive = false;
    m_textInputBackWasPressed = false;
    m_steamKeyboardOpen = false;
    m_steamKeyboardSawInactive = false;
    clearTextInputTarget();
    m_textInputGraceUntilMs = QDateTime::currentMSecsSinceEpoch() + 800;
    syncTextInputActiveProperty();

    if (wasActive) {
        emit textInputActiveChanged();
    }

    if (wasOpen) {
        QTimer::singleShot(200, this, []() {
            openSteamDeeplink("steam://close/keyboard");
        });
    }
    scheduleWindowReactivateAfterOsk();
    m_oskInputRecoveryUntilMs = QDateTime::currentMSecsSinceEpoch() + 5000;
    qInfo() << "InputManager: web keyboard session ended";
}

void InputManager::closeSteamKeyboard()
{
    if (m_webTextInputSession) {
        closeWebKeyboardSession();
        return;
    }

    const bool wasActive = m_textInputActive;
    const bool wasOpen = m_steamKeyboardOpen;

    if (!wasActive && !wasOpen) {
        return;
    }

    m_textInputActive = false;
    m_textInputBackWasPressed = false;
    m_steamKeyboardOpen = false;
    m_steamKeyboardSawInactive = false;
    clearTextInputTarget();
    m_textInputGraceUntilMs = QDateTime::currentMSecsSinceEpoch() + 800;
    syncTextInputActiveProperty();

    if (wasActive) {
        emit textInputActiveChanged();
    }

    if (!wasOpen) {
        return;
    }

    QTimer::singleShot(200, this, []() {
        openSteamDeeplink("steam://close/keyboard");
    });

    m_oskInputRecoveryUntilMs = QDateTime::currentMSecsSinceEpoch() + 5000;

    QTimer::singleShot(600, this, [this]() {
        if (m_textInputActive) {
            return;
        }
        if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
            if (QWindow *w = targetWindow()) {
                w->requestActivate();
            }
        }
    });
    qInfo() << "InputManager: Steam keyboard close requested";
}

void InputManager::acknowledgeSteamKeyboardClosed()
{
    if (m_webTextInputSession) {
        closeWebKeyboardSession();
        return;
    }
    if (!m_textInputActive && !m_steamKeyboardOpen) {
        return;
    }

    const bool wasActive = m_textInputActive;
    m_textInputActive = false;
    m_textInputBackWasPressed = false;
    m_steamKeyboardOpen = false;
    m_steamKeyboardSawInactive = false;
    clearTextInputTarget();
    m_textInputGraceUntilMs = QDateTime::currentMSecsSinceEpoch() + 800;
    syncTextInputActiveProperty();
    if (wasActive) {
        emit textInputActiveChanged();
    }

    m_oskInputRecoveryUntilMs = QDateTime::currentMSecsSinceEpoch() + 5000;

    QTimer::singleShot(600, this, [this]() {
        if (m_textInputActive) {
            return;
        }
        if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
            if (QWindow *w = targetWindow()) {
                w->requestActivate();
            }
        }
    });
    qInfo() << "InputManager: Steam keyboard closed acknowledged (no deeplink)";
}

void InputManager::maybeAcknowledgeSteamKeyboardClosed(const char *reason)
{
    if (!m_textInputActive && !m_steamKeyboardOpen) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (m_steamKeyboardOpenedAtMs > 0 && nowMs - m_steamKeyboardOpenedAtMs < 700) {
        return;
    }

    qInfo().noquote() << QStringLiteral("InputManager: OSK self-dismiss detected (%1)")
                             .arg(QString::fromLatin1(reason ? reason : "unknown"));
    acknowledgeSteamKeyboardClosed();
}

void InputManager::installSteamKeyboardDismissWatchers()
{
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (!m_textInputActive && !m_steamKeyboardOpen) {
            return;
        }

        if (state != Qt::ApplicationActive) {
            m_steamKeyboardSawInactive = true;
            if (m_webTextInputSession && !m_steamKeyboardOpen) {
                m_steamKeyboardOpen = true;
                m_steamKeyboardOpenedAtMs = QDateTime::currentMSecsSinceEpoch();
                m_webKeyboardClosedAtMs = 0;
                m_lastTextInputAtMs = 0;
                m_lastAppendAtMs = 0;
                m_lastAppendChunk.clear();
                qInfo() << "InputManager: web keyboard session: OSK open detected via app inactive";
            }
            return;
        }

        if (!m_steamKeyboardSawInactive) {
            return;
        }

        QTimer::singleShot(250, this, [this]() {
            if (QGuiApplication::applicationState() != Qt::ApplicationActive) {
                return;
            }
            maybeAcknowledgeSteamKeyboardClosed("applicationActive");
        });
    });

    connect(qGuiApp, &QGuiApplication::focusWindowChanged, this, [this](QWindow *window) {
        if (!m_textInputActive && !m_steamKeyboardOpen) {
            return;
        }
        if (!window) {
            m_steamKeyboardSawInactive = true;
            return;
        }

        QTimer::singleShot(250, this, [this, window]() {
            if (QGuiApplication::focusWindow() != window) {
                return;
            }
            if (!m_steamKeyboardSawInactive) {
                return;
            }
            maybeAcknowledgeSteamKeyboardClosed("focusWindow");
        });
    });
}

void InputManager::setTextInputActive(bool active)
{
    if (active) {
        if (m_textInputActive) {
            return;
        }
        m_textInputActive = true;
        syncTextInputActiveProperty();
        emit textInputActiveChanged();
        return;
    }

    if (m_webTextInputSession) {
        closeWebKeyboardSession();
        return;
    }
    closeSteamKeyboard();
}

void InputManager::pollGamepad()
{
    m_suppressPageScopeInputUntilNextPoll = false;

    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_CONTROLLERDEVICEADDED) {
            qInfo().noquote() << QStringLiteral("InputManager: SDL_CONTROLLERDEVICEADDED index=%1")
                                    .arg(event.cdevice.which);
            openMissingPads();
        } else if (event.type == SDL_JOYDEVICEADDED) {
            qInfo().noquote() << QStringLiteral("InputManager: SDL_JOYDEVICEADDED index=%1")
                                    .arg(event.jdevice.which);
            openMissingPads();
        } else if (event.type == SDL_CONTROLLERDEVICEREMOVED) {
            qInfo().noquote() << QStringLiteral("InputManager: SDL_CONTROLLERDEVICEREMOVED instance=%1")
                                    .arg(event.cdevice.which);
            closePadByInstanceId(event.cdevice.which);
        } else if (event.type == SDL_JOYDEVICEREMOVED) {
            qInfo().noquote() << QStringLiteral("InputManager: SDL_JOYDEVICEREMOVED instance=%1")
                                    .arg(event.jdevice.which);
            closePadByInstanceId(event.jdevice.which);
        }
    }

    const auto padCountBefore = m_pads.size();
    openMissingPads();
    dropSteamVirtualPadsIfRealPresent();
    if (m_pads.size() != padCountBefore) {
        persistGamepadCatalog();
        logSdlJoystickList("pad-count-changed");
        logGamepadInventory("pad-count-changed");
    }
    if (m_pads.empty()) {
        if (m_controller || m_joystick || m_connectedGamepadKind != InputDeviceKind::Unknown
            || !m_inputDeviceName.isEmpty()) {
            qInfo() << "InputManager: no open pads; keeping last layout"
                    << m_resolvedButtonIconLayout;
            m_controller = nullptr;
            m_joystick = nullptr;
            m_controllerInstanceId = -1;
            resetExclusiveInputState();
            updateConnectedGamepadInfo();
        }
        return;
    }

    if (padIndexByInstanceKey(m_exclusiveInstanceKey) < 0) {
        qInfo().noquote() << QStringLiteral(
            "InputManager: exclusive pad missing, rechoose last=%1")
                                .arg(m_exclusiveInstanceKey.isEmpty() ? QStringLiteral("<none>")
                                                                      : m_exclusiveInstanceKey);
        assignExclusive(GamepadCatalog::chooseExclusiveInstanceId(connectedCatalogRecords(),
                                                                  m_exclusiveInstanceKey),
                        true);
    }
    maybeTakeOverFromOtherPads();
    bindExclusiveAliases();

    if (!hasOpenController()) {
        return;
    }

    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    qreal deltaSeconds = 0.016;
    if (m_lastPollMs > 0) {
        deltaSeconds = qBound(0.001, (nowMs - m_lastPollMs) / 1000.0, 0.05);
    }
    m_lastPollMs = nowMs;

    m_leftAxisX = normalizeAxis(axisValue(SDL_CONTROLLER_AXIS_LEFTX));
    m_leftAxisY = normalizeAxis(axisValue(SDL_CONTROLLER_AXIS_LEFTY));
    m_rightAxisX = normalizeAxis(axisValue(SDL_CONTROLLER_AXIS_RIGHTX));
    m_rightAxisY = normalizeAxis(axisValue(SDL_CONTROLLER_AXIS_RIGHTY));

    const bool leftTriggerPressed =
        GamepadTrigger::isHeld(axisValue(SDL_CONTROLLER_AXIS_TRIGGERLEFT), m_leftTriggerPressed)
        || (!m_controller && joystickButtonPressed(6));
    const bool rightTriggerPressed =
        GamepadTrigger::isHeld(axisValue(SDL_CONTROLLER_AXIS_TRIGGERRIGHT), m_rightTriggerPressed)
        || (!m_controller && joystickButtonPressed(7));
    const bool backPressed = buttonPressed(SDL_CONTROLLER_BUTTON_B);
    const bool actionPressed = buttonPressed(SDL_CONTROLLER_BUTTON_A);
    const bool leftShoulderPressed = buttonPressed(SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
    const bool rightShoulderPressed = buttonPressed(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    const bool leftStickActive = std::abs(m_leftAxisX) > kTriggerMotionThreshold || std::abs(m_leftAxisY) > kTriggerMotionThreshold;
    const bool rightStickActive = std::abs(m_rightAxisX) > kTriggerMotionThreshold || std::abs(m_rightAxisY) > kTriggerMotionThreshold;
    const bool anyStickActive = leftStickActive || rightStickActive;
    const bool resumedStickMotion = anyStickActive && !m_anyStickWasActive;

    const auto syncButtonStatesWithoutDispatch = [this, backPressed, actionPressed, leftShoulderPressed, rightShoulderPressed]() {
        for (int buttonIndex = 0; buttonIndex < SDL_CONTROLLER_BUTTON_MAX; ++buttonIndex) {
            if (buttonIndex == SDL_CONTROLLER_BUTTON_B) {
                m_buttonStates[buttonIndex] = backPressed;
                continue;
            }
            if (buttonIndex == SDL_CONTROLLER_BUTTON_A) {
                m_buttonStates[buttonIndex] = actionPressed;
                continue;
            }
            if (buttonIndex == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
                m_buttonStates[buttonIndex] = leftShoulderPressed;
                continue;
            }
            if (buttonIndex == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER) {
                m_buttonStates[buttonIndex] = rightShoulderPressed;
                continue;
            }

            const bool pressed = buttonPressed(static_cast<SDL_GameControllerButton>(buttonIndex));
            m_buttonStates[buttonIndex] = pressed;
        }
    };

    const auto consumeFrameWithoutDispatch = [&]() {
        m_anyStickWasActive = anyStickActive;
        m_focusRepeatAtMs.fill(0);
        m_dpadRepeatAtMs.fill(0);
        m_cursorRemainder = QPointF();
        m_edgeScrollRemainder = QPointF();
        m_scrollRemainder = QPointF();
        updateGamepadButtonEdges(false,
                                 actionPressed,
                                 backPressed,
                                 leftShoulderPressed,
                                 rightShoulderPressed,
                                 leftTriggerPressed,
                                 rightTriggerPressed);
        syncButtonStatesWithoutDispatch();
    };

    if (oskBlockingSession()) {
        if (m_webTextInputSession && m_steamKeyboardOpen) {
            const bool rescueGraceDone = m_steamKeyboardOpenedAtMs == 0
                || nowMs - m_steamKeyboardOpenedAtMs >= 1000;
            const bool rescueTypingQuiet = m_lastTextInputAtMs == 0
                || nowMs - m_lastTextInputAtMs >= 1000;
            const bool leftStickRising = leftStickActive && !m_leftStickWasActive;
            m_leftStickWasActive = leftStickActive;
            const bool rescueInputEdge = leftStickRising
                || actionPressed || backPressed
                || leftShoulderPressed || rightShoulderPressed;
            if (rescueGraceDone && rescueTypingQuiet && rescueInputEdge) {
                qInfo() << "InputManager: stuck OSK session rescued on input edge";
                acknowledgeSteamKeyboardClosed();
            }
        }

        if (backPressed && !m_textInputBackWasPressed) {
            if (m_webTextInputSession) {
                closeWebKeyboardSession();
                dispatchPageAction(QStringLiteral("back"));
                m_textInputBackWasPressed = backPressed;
                m_backPressedAtMs = nowMs;
                m_backLongPressHandled = false;
                m_backConsumed = true;
                m_backConsumePending = false;
                consumeFrameWithoutDispatch();
                return;
            } else {
                closeSteamKeyboard();
                m_textInputBackWasPressed = backPressed;
                m_backConsumed = true;
                m_backConsumePending = false;
                consumeFrameWithoutDispatch();
                return;
            }
        }
        m_textInputBackWasPressed = backPressed;

        const bool leftStickRising = leftStickActive && !m_leftStickWasActive;
        m_leftStickWasActive = leftStickActive;
        const bool anyButtonEdge = actionPressed || backPressed
            || leftShoulderPressed || rightShoulderPressed;
        if (!m_webTextInputSession && m_textInputActive && (leftStickRising || anyButtonEdge)) {
            const bool openGraceDone = m_steamKeyboardOpenedAtMs == 0
                || nowMs - m_steamKeyboardOpenedAtMs >= 1000;
            const bool typingQuiet = m_lastTextInputAtMs == 0
                ? openGraceDone
                : (nowMs - m_lastTextInputAtMs >= 600);
            if (openGraceDone && typingQuiet) {
                maybeAcknowledgeSteamKeyboardClosed(anyButtonEdge ? "buttonPressAfterQuiet"
                                                                  : "leftStickRisingAfterQuiet");
            }
        }

        if (oskBlockingSession()) {
            m_anyStickWasActive = anyStickActive;
            m_focusRepeatAtMs.fill(0);
            m_dpadRepeatAtMs.fill(0);
            m_cursorRemainder = QPointF();
            m_edgeScrollRemainder = QPointF();
            m_scrollRemainder = QPointF();
            setPreciseMode(false);
            setTurboMode(false);

            updateGamepadButtonEdges(false,
                                     actionPressed,
                                     backPressed,
                                     leftShoulderPressed,
                                     rightShoulderPressed,
                                     leftTriggerPressed,
                                     rightTriggerPressed);
            syncButtonStatesWithoutDispatch();
            return;
        }
    }

    const bool inOskRecovery = m_oskInputRecoveryUntilMs > 0 && nowMs < m_oskInputRecoveryUntilMs;
    if (!applicationAcceptsControllerInput() && !inOskRecovery) {
        if (!m_controllerInputWasGated) {
            m_controllerInputWasGated = true;
            qInfo().noquote() << QStringLiteral("InputManager: controller input gated: appState=%1 focusWindow=%2 activeWindow=%3")
                                     .arg(QGuiApplication::applicationState())
                                     .arg(reinterpret_cast<quintptr>(QGuiApplication::focusWindow()))
                                     .arg(reinterpret_cast<quintptr>(QApplication::activeWindow()));
        }
        m_anyStickWasActive = anyStickActive;
        m_focusRepeatAtMs.fill(0);
        m_dpadRepeatAtMs.fill(0);
        m_cursorRemainder = QPointF();
        m_edgeScrollRemainder = QPointF();
        m_scrollRemainder = QPointF();
        setPreciseMode(false);
        setTurboMode(false);

        m_leftTriggerPressed = leftTriggerPressed;
        m_rightTriggerPressed = rightTriggerPressed;
        m_leftShoulderPressed = leftShoulderPressed;
        m_rightShoulderPressed = rightShoulderPressed;
        m_leftTriggerPressedAtMs = leftTriggerPressed ? nowMs : 0;
        m_rightTriggerPressedAtMs = rightTriggerPressed ? nowMs : 0;
        m_leftShoulderPressedAtMs = leftShoulderPressed ? nowMs : 0;
        m_rightShoulderPressedAtMs = rightShoulderPressed ? nowMs : 0;
        m_leftTriggerUsedForMotion = leftTriggerPressed && anyStickActive;
        m_rightTriggerUsedForMotion = rightTriggerPressed && anyStickActive;
        m_leftShoulderChordUsed = false;
        m_rightShoulderChordUsed = false;
        m_backPressedAtMs = backPressed ? nowMs : 0;
        m_backLongPressHandled = false;

        updateGamepadButtonEdges(false,
                                 actionPressed,
                                 backPressed,
                                 leftShoulderPressed,
                                 rightShoulderPressed,
                                 leftTriggerPressed,
                                 rightTriggerPressed);
        syncButtonStatesWithoutDispatch();
        return;
    }
    if (m_controllerInputWasGated) {
        m_controllerInputWasGated = false;
    }

    m_anyStickWasActive = anyStickActive;

    if (leftTriggerPressed && !m_leftTriggerPressed) {
        m_leftTriggerPressedAtMs = nowMs;
        m_leftTriggerUsedForMotion = false;
    }
    if (rightTriggerPressed && !m_rightTriggerPressed) {
        m_rightTriggerPressedAtMs = nowMs;
        m_rightTriggerUsedForMotion = false;
    }

    updateGamepadButtonEdges(true,
                             actionPressed,
                             backPressed,
                             leftShoulderPressed,
                             rightShoulderPressed,
                             leftTriggerPressed,
                             rightTriggerPressed);

    if (!leftTriggerPressed && m_leftTriggerPressed) {
        if (!m_leftTriggerUsedForMotion
            && m_leftTriggerPressedAtMs > 0
            && nowMs - m_leftTriggerPressedAtMs <= kTriggerShortPressMaxMs) {
            dispatchPageAction(QStringLiteral("previousSession"));
        }
        m_leftTriggerPressedAtMs = 0;
        m_leftTriggerUsedForMotion = false;
    }
    if (!rightTriggerPressed && m_rightTriggerPressed) {
        if (!m_rightTriggerUsedForMotion
            && m_rightTriggerPressedAtMs > 0
            && nowMs - m_rightTriggerPressedAtMs <= kTriggerShortPressMaxMs) {
            dispatchPageAction(QStringLiteral("nextSession"));
        }
        m_rightTriggerPressedAtMs = 0;
        m_rightTriggerUsedForMotion = false;
    }
    const bool leftTriggerRisingEdge = leftTriggerPressed && !m_leftTriggerPressed;
    const bool rightTriggerRisingEdge = rightTriggerPressed && !m_rightTriggerPressed;

    m_leftTriggerPressed = leftTriggerPressed;
    m_rightTriggerPressed = rightTriggerPressed;

    const bool preciseModeActive = leftTriggerPressed && anyStickActive && !rightTriggerPressed;
    const bool turboModeActive = rightTriggerPressed && anyStickActive;
    if (leftTriggerPressed && anyStickActive) {
        m_leftTriggerUsedForMotion = true;
    }
    if (rightTriggerPressed && anyStickActive) {
        m_rightTriggerUsedForMotion = true;
    }
    setPreciseMode(preciseModeActive);
    setTurboMode(turboModeActive);

    if (backPressed && !m_buttonStates[SDL_CONTROLLER_BUTTON_B]) {
        m_backPressedAtMs = nowMs;
        m_backLongPressHandled = false;
        m_backConsumed = m_backConsumePending;
        m_backConsumePending = false;
    } else if (backPressed && !m_backConsumed && !m_backLongPressHandled && m_backPressedAtMs > 0 && nowMs - m_backPressedAtMs >= kBackLongPressMs) {
        emit gamepadButtonLongPressed(gamepadButtonName(GamepadButton::B));
        if (isBrowserPointerRoute()) {
            dispatchPageAction(QStringLiteral("closeSession"));
        }
        m_backLongPressHandled = true;
    } else if (!backPressed && m_buttonStates[SDL_CONTROLLER_BUTTON_B]) {
        if (!m_backLongPressHandled && !m_backConsumed) {
            dispatchPageAction(QStringLiteral("back"));
        }
        m_backPressedAtMs = 0;
        m_backLongPressHandled = false;
        m_backConsumed = false;
        m_backConsumePending = false;
    }

    const bool leftShoulderRisingEdge = leftShoulderPressed && !m_buttonStates[SDL_CONTROLLER_BUTTON_LEFTSHOULDER];
    const bool rightShoulderRisingEdge = rightShoulderPressed && !m_buttonStates[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];
    const bool bothShouldersPressed = leftShoulderPressed && rightShoulderPressed;
    const bool bothWerePressedLastFrame = m_buttonStates[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]
                                          && m_buttonStates[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];

    if (bothShouldersPressed && !bothWerePressedLastFrame) {
        m_leftShoulderChordUsed = true;
        m_rightShoulderChordUsed = true;
        dispatchPageAction(QStringLiteral("togglePageOverlay"));
    } else {
        if (leftShoulderRisingEdge) {
            m_leftShoulderPressedAtMs = nowMs;
            m_leftShoulderChordUsed = false;
        } else if (!leftShoulderPressed && m_buttonStates[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]) {
            if (!m_leftShoulderChordUsed
                && m_leftShoulderPressedAtMs > 0
                && nowMs - m_leftShoulderPressedAtMs <= kShoulderShortPressMaxMs) {
                dispatchPageAction(QStringLiteral("previousTab"));
            }
            m_leftShoulderPressedAtMs = 0;
            m_leftShoulderChordUsed = false;
        }

        if (rightShoulderRisingEdge) {
            m_rightShoulderPressedAtMs = nowMs;
            m_rightShoulderChordUsed = false;
        } else if (!rightShoulderPressed && m_buttonStates[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER]) {
            if (!m_rightShoulderChordUsed
                && m_rightShoulderPressedAtMs > 0
                && nowMs - m_rightShoulderPressedAtMs <= kShoulderShortPressMaxMs) {
                dispatchPageAction(QStringLiteral("nextTab"));
            }
            m_rightShoulderPressedAtMs = 0;
            m_rightShoulderChordUsed = false;
        }
    }
    m_leftShoulderPressed = leftShoulderPressed;
    m_rightShoulderPressed = rightShoulderPressed;

    if (leftShoulderRisingEdge && !bothShouldersPressed) {
        dispatchLbDpadChordForHeldDirections();
    }

    if (rightShoulderRisingEdge && !bothShouldersPressed) {
        dispatchRbDpadChordForHeldDirections();
    }

    if (leftTriggerRisingEdge) {
        dispatchLtDpadChordForHeldDirections();
    }

    if (rightTriggerRisingEdge) {
        dispatchRtDpadChordForHeldDirections();
    }

    if (actionPressed && !m_buttonStates[SDL_CONTROLLER_BUTTON_A]) {
        if (isBrowserPointerRoute()) {
            handleButtonPress(SDL_CONTROLLER_BUTTON_A);
        }
    }
    m_buttonStates[SDL_CONTROLLER_BUTTON_A] = actionPressed;

    handleDpadButton(nowMs, SDL_CONTROLLER_BUTTON_DPAD_UP, 0, buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_UP));
    handleDpadButton(nowMs, SDL_CONTROLLER_BUTTON_DPAD_DOWN, 1, buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN));
    handleDpadButton(nowMs, SDL_CONTROLLER_BUTTON_DPAD_LEFT, 2, buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT));
    handleDpadButton(nowMs, SDL_CONTROLLER_BUTTON_DPAD_RIGHT, 3, buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT));

    for (int buttonIndex = 0; buttonIndex < SDL_CONTROLLER_BUTTON_MAX; ++buttonIndex) {
        if (buttonIndex == SDL_CONTROLLER_BUTTON_B
            || buttonIndex == SDL_CONTROLLER_BUTTON_A
            || buttonIndex == SDL_CONTROLLER_BUTTON_LEFTSHOULDER
            || buttonIndex == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER
            || buttonIndex == SDL_CONTROLLER_BUTTON_DPAD_UP
            || buttonIndex == SDL_CONTROLLER_BUTTON_DPAD_DOWN
            || buttonIndex == SDL_CONTROLLER_BUTTON_DPAD_LEFT
            || buttonIndex == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
            m_buttonStates[buttonIndex] = buttonIndex == SDL_CONTROLLER_BUTTON_B
                ? backPressed
                : (buttonIndex == SDL_CONTROLLER_BUTTON_A
                    ? actionPressed
                    : (buttonIndex == SDL_CONTROLLER_BUTTON_LEFTSHOULDER ? leftShoulderPressed
                                                                         : (buttonIndex == SDL_CONTROLLER_BUTTON_RIGHTSHOULDER ? rightShoulderPressed
                                                                                                                               : m_buttonStates[buttonIndex])));
            continue;
        }

        const bool pressed = buttonPressed(static_cast<SDL_GameControllerButton>(buttonIndex));
        if (pressed && !m_buttonStates[buttonIndex]) {
            if (isBrowserPointerRoute()) {
                handleButtonPress(static_cast<SDL_GameControllerButton>(buttonIndex));
            }
        }
        m_buttonStates[buttonIndex] = pressed;
    }

    Q_UNUSED(nowMs);
    if (anyStickActive || actionPressed || backPressed || leftShoulderPressed || rightShoulderPressed
        || leftTriggerPressed || rightTriggerPressed) {
        noteGamepadActivity();
    }

    if (isUiFocusRoute()) {
        handleFocusMotion(nowMs);
        return;
    }

    if (resumedStickMotion) {
        refreshSystemCursor();
    }

    handleCursorMotion(deltaSeconds);
    handleCursorScroll(deltaSeconds);
}

void InputManager::applySteamInputBypassHints()
{
    const bool launchedBySteam = qEnvironmentVariableIsSet("SteamEnv")
        || qEnvironmentVariableIsSet("SteamDeck")
        || qEnvironmentVariableIsSet("SteamAppId");
    if (launchedBySteam) {
        SDL_SetHintWithPriority("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", SDL_HINT_OVERRIDE);
        SDL_SetHintWithPriority("SDL_JOYSTICK_ALLOW_STEAM_VIRTUAL_GAMEPAD", "1", SDL_HINT_OVERRIDE);
#ifdef SDL_HINT_JOYSTICK_ALLOW_STEAM_VIRTUAL_GAMEPAD
        SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_STEAM_VIRTUAL_GAMEPAD, "1", SDL_HINT_OVERRIDE);
#endif
    }
#ifdef SDL_HINT_JOYSTICK_HIDAPI
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI, "1", SDL_HINT_OVERRIDE);
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK, "1", SDL_HINT_OVERRIDE);
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS4
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS4, "1", SDL_HINT_OVERRIDE);
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_PS5
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_PS5, "1", SDL_HINT_OVERRIDE);
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_SWITCH
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_SWITCH, "1", SDL_HINT_OVERRIDE);
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_JOY_CONS, "1", SDL_HINT_OVERRIDE);
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_XBOX
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_HIDAPI_XBOX, "1", SDL_HINT_OVERRIDE);
#endif
    SDL_SetHintWithPriority(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1", SDL_HINT_OVERRIDE);

    SDL_version compiled{};
    SDL_VERSION(&compiled);
    SDL_version linked{};
    SDL_GetVersion(&linked);
    const char *allowVirtual = SDL_GetHint("SDL_GAMECONTROLLER_ALLOW_STEAM_VIRTUAL_GAMEPAD");
    QString ignoreDevices = qEnvironmentVariable("SDL_GAMECONTROLLER_IGNORE_DEVICES");
    if (ignoreDevices.isEmpty() && SDL_GetHint("SDL_GAMECONTROLLER_IGNORE_DEVICES")) {
        ignoreDevices = QString::fromUtf8(SDL_GetHint("SDL_GAMECONTROLLER_IGNORE_DEVICES"));
    }
    if (ignoreDevices.size() > 120) {
        ignoreDevices = ignoreDevices.left(120) + QStringLiteral("...");
    }
    qInfo().noquote() << QStringLiteral(
        "InputManager: steam-input bypass hints applied launchedBySteam=%1 SteamDeck=%2 SteamEnv=%3 "
        "ALLOW_VIRTUAL=%4 IGNORE_DEVICES=%5 HIDAPI=%6 HIDAPI_STEAMDECK=%7")
                          .arg(launchedBySteam ? QStringLiteral("yes") : QStringLiteral("no"),
                               qEnvironmentVariable("SteamDeck"),
                               qEnvironmentVariable("SteamEnv"),
                               allowVirtual ? QString::fromUtf8(allowVirtual) : QStringLiteral("<unset>"),
                               ignoreDevices.isEmpty() ? QStringLiteral("<unset>") : ignoreDevices,
#ifdef SDL_HINT_JOYSTICK_HIDAPI
                               QString::fromUtf8(SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI)),
#else
                               QStringLiteral("<hint-missing>"),
#endif
#ifdef SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK
                               QString::fromUtf8(SDL_GetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAMDECK))
#else
                               QStringLiteral("<hint-missing>")
#endif
                          );
    qInfo().noquote() << QStringLiteral("InputManager: sdl version compiled=%1.%2.%3 linked=%4.%5.%6")
                            .arg(compiled.major)
                            .arg(compiled.minor)
                            .arg(compiled.patch)
                            .arg(linked.major)
                            .arg(linked.minor)
                            .arg(linked.patch);
}

void InputManager::loadGamepadCatalog()
{
    m_knownGamepads = SettingsStore::instance().rememberedGamepads();
    m_exclusiveInstanceKey = SettingsStore::instance().lastExclusiveGamepadId();
    m_resolvedButtonIconLayout = SettingsStore::instance().lastButtonIconLayout();
    qInfo().noquote() << QStringLiteral(
        "InputManager: loaded catalog remembered=%1 lastExclusive=%2 lastLayout=%3")
                          .arg(m_knownGamepads.size())
                          .arg(m_exclusiveInstanceKey.isEmpty() ? QStringLiteral("<none>")
                                                                : m_exclusiveInstanceKey)
                          .arg(m_resolvedButtonIconLayout);
    for (int index = 0; index < m_knownGamepads.size(); ++index) {
        const GamepadCatalog::DeviceRecord &record = m_knownGamepads[index];
        qInfo().noquote() << QStringLiteral(
            "InputManager: remembered[%1] name=%2 id=%3 kind=%4 override=%5 lastUsedMs=%6 builtin=%7")
                              .arg(index)
                              .arg(record.name,
                                   record.instanceId,
                                   record.detectedKindId,
                                   record.layoutOverride,
                                   QString::number(record.lastUsedMs),
                                   record.builtin ? QStringLiteral("yes") : QStringLiteral("no"));
    }
}

void InputManager::persistGamepadCatalog() const
{
    SettingsStore::instance().setRememberedGamepads(m_knownGamepads);
    SettingsStore::instance().setLastExclusiveGamepadId(m_exclusiveInstanceKey);
    SettingsStore::instance().setLastButtonIconLayout(m_resolvedButtonIconLayout);
}

void InputManager::logSdlJoystickList(const char *reason) const
{
    const int joystickCount = SDL_NumJoysticks();
    qInfo().noquote() << QStringLiteral("InputManager: sdl joysticks (%1) count=%2")
                            .arg(QString::fromUtf8(reason))
                            .arg(joystickCount);
    for (int index = 0; index < joystickCount; ++index) {
        const char *name = SDL_JoystickNameForIndex(index);
        const quint16 vendor = SDL_JoystickGetDeviceVendor(index);
        const quint16 product = SDL_JoystickGetDeviceProduct(index);
        const SDL_JoystickID instanceId = SDL_JoystickGetDeviceInstanceID(index);
        char guidBuffer[33] = {};
        SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(index), guidBuffer, sizeof(guidBuffer));
        qInfo().noquote() << QStringLiteral(
            "InputManager: sdl[%1] name=%2 vidpid=%3 instance=%4 guid=%5 gamecontroller=%6 sdlType=%7")
                              .arg(index)
                              .arg(name ? QString::fromUtf8(name) : QStringLiteral("<null>"),
                                   formatVidPid(vendor, product),
                                   QString::number(instanceId),
                                   QString::fromLatin1(guidBuffer),
                                   SDL_IsGameController(index) ? QStringLiteral("yes")
                                                               : QStringLiteral("no"),
                                   QString::number(SDL_IsGameController(index)
                                                       ? static_cast<int>(SDL_GameControllerTypeForIndex(index))
                                                       : 0));
    }
}

void InputManager::logGamepadInventory(const char *reason) const
{
    qInfo().noquote() << QStringLiteral(
        "InputManager: inventory (%1) open=%2 exclusive=%3 layout=%4 remembered=%5")
                          .arg(QString::fromUtf8(reason))
                          .arg(static_cast<int>(m_pads.size()))
                          .arg(m_exclusiveInstanceKey.isEmpty() ? QStringLiteral("<none>")
                                                                : m_exclusiveInstanceKey,
                               m_resolvedButtonIconLayout,
                               QString::number(m_knownGamepads.size()));
    for (int index = 0; index < static_cast<int>(m_pads.size()); ++index) {
        const OpenedPad &pad = m_pads[static_cast<size_t>(index)];
        const bool exclusive = pad.instanceKey == m_exclusiveInstanceKey;
        qInfo().noquote() << QStringLiteral(
            "InputManager: pad[%1] name=%2 key=%3 vidpid=%4 kind=%5 virtual=%6 builtin=%7 exclusive=%8 gc=%9")
                              .arg(index)
                              .arg(pad.name,
                                   pad.instanceKey,
                                   formatVidPid(pad.vendor, pad.product),
                                   inputDeviceKindIdFor(pad.detectedKind),
                                   pad.virtualPad ? QStringLiteral("yes") : QStringLiteral("no"),
                                   pad.builtin ? QStringLiteral("yes") : QStringLiteral("no"),
                                   exclusive ? QStringLiteral("yes") : QStringLiteral("no"),
                                   pad.controller ? QStringLiteral("yes") : QStringLiteral("no"));
    }
}

QString InputManager::joystickGuidString(SDL_Joystick *joystick)
{
    if (!joystick) {
        return {};
    }
    char buffer[33] = {};
    SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(joystick), buffer, sizeof(buffer));
    return QString::fromLatin1(buffer);
}

int InputManager::padIndexByInstanceId(SDL_JoystickID instanceId) const
{
    for (int index = 0; index < static_cast<int>(m_pads.size()); ++index) {
        if (m_pads[static_cast<size_t>(index)].instanceId == instanceId) {
            return index;
        }
    }
    return -1;
}

int InputManager::padIndexByInstanceKey(const QString &instanceKey) const
{
    if (instanceKey.isEmpty()) {
        return -1;
    }
    for (int index = 0; index < static_cast<int>(m_pads.size()); ++index) {
        if (m_pads[static_cast<size_t>(index)].instanceKey == instanceKey) {
            return index;
        }
    }
    const QString guid = GamepadCatalog::guidOf(instanceKey);
    for (int index = 0; index < static_cast<int>(m_pads.size()); ++index) {
        if (m_pads[static_cast<size_t>(index)].guid == guid) {
            return index;
        }
    }
    return -1;
}

QString InputManager::makeInstanceKey(const QString &guid, SDL_JoystickID instanceId) const
{
    if (guid.isEmpty()) {
        return QStringLiteral("instance-%1").arg(instanceId);
    }
    for (const OpenedPad &pad : m_pads) {
        if (pad.guid == guid) {
            return guid + QLatin1Char('#') + QString::number(instanceId);
        }
    }
    return guid;
}

QVector<QString> InputManager::connectedInstanceIds() const
{
    QVector<QString> ids;
    ids.reserve(static_cast<int>(m_pads.size()));
    for (const OpenedPad &pad : m_pads) {
        ids.push_back(pad.instanceKey);
    }
    return ids;
}

QVector<GamepadCatalog::DeviceRecord> InputManager::connectedCatalogRecords() const
{
    QVector<GamepadCatalog::DeviceRecord> records;
    for (const OpenedPad &pad : m_pads) {
        GamepadCatalog::DeviceRecord record;
        record.instanceId = pad.instanceKey;
        record.name = pad.name;
        record.detectedKindId = inputDeviceKindIdFor(pad.detectedKind);
        record.builtin = pad.builtin;
        const int knownIndex = GamepadCatalog::findRecordIndex(m_knownGamepads, pad.instanceKey);
        if (knownIndex >= 0) {
            record.layoutOverride = m_knownGamepads[knownIndex].layoutOverride;
            record.lastUsedMs = m_knownGamepads[knownIndex].lastUsedMs;
            record.detectedKindId = m_knownGamepads[knownIndex].detectedKindId;
        }
        records.push_back(record);
    }
    return records;
}

void InputManager::upsertOpenPadsIntoCatalog()
{
    const QVector<QString> connectedIds = connectedInstanceIds();
    for (const OpenedPad &pad : m_pads) {
        GamepadCatalog::DeviceRecord incoming;
        incoming.instanceId = pad.instanceKey;
        incoming.name = pad.name;
        incoming.detectedKindId = inputDeviceKindIdFor(pad.detectedKind);
        incoming.builtin = pad.builtin;
        GamepadCatalog::upsertDevice(m_knownGamepads, incoming, connectedIds);
    }
}

void InputManager::destroyPadHandles(int padIndex)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(m_pads.size())) {
        return;
    }
    OpenedPad &pad = m_pads[static_cast<size_t>(padIndex)];
    if (pad.controller) {
        SDL_GameControllerClose(pad.controller);
        pad.controller = nullptr;
        pad.joystick = nullptr;
    } else if (pad.joystick) {
        SDL_JoystickClose(pad.joystick);
        pad.joystick = nullptr;
    }
}

void InputManager::closePadByInstanceId(SDL_JoystickID instanceId)
{
    const int index = padIndexByInstanceId(instanceId);
    if (index < 0) {
        qInfo().noquote() << QStringLiteral(
            "InputManager: closePad ignored unknown instance=%1 (already closed or duplicate remove)")
                                .arg(instanceId);
        return;
    }
    const OpenedPad &removed = m_pads[static_cast<size_t>(index)];
    qInfo().noquote() << QStringLiteral(
        "InputManager: closePad name=%1 key=%2 instance=%3 exclusive=%4 remainingAfter=%5")
                          .arg(removed.name,
                               removed.instanceKey,
                               QString::number(instanceId),
                               removed.instanceKey == m_exclusiveInstanceKey ? QStringLiteral("yes")
                                                                             : QStringLiteral("no"),
                               QString::number(static_cast<int>(m_pads.size()) - 1));
    const QString removedKey = removed.instanceKey;
    destroyPadHandles(index);
    m_pads.erase(m_pads.begin() + index);
    if (m_controllerInstanceId == instanceId) {
        m_controller = nullptr;
        m_joystick = nullptr;
        m_controllerInstanceId = -1;
    }
    if (removedKey == m_exclusiveInstanceKey || padIndexByInstanceKey(m_exclusiveInstanceKey) < 0) {
        assignExclusive(GamepadCatalog::chooseExclusiveInstanceId(connectedCatalogRecords(), removedKey),
                        true);
    }
    logGamepadInventory("after-close");
}

void InputManager::closeAllPads()
{
    for (int index = static_cast<int>(m_pads.size()) - 1; index >= 0; --index) {
        destroyPadHandles(index);
    }
    m_pads.clear();
    m_controller = nullptr;
    m_joystick = nullptr;
    m_controllerInstanceId = -1;
    resetExclusiveInputState();
}

void InputManager::openMissingPads()
{
    const int joystickCount = SDL_NumJoysticks();
    for (int index = 0; index < joystickCount; ++index) {
        const SDL_JoystickID instanceId = SDL_JoystickGetDeviceInstanceID(index);
        if (instanceId >= 0 && padIndexByInstanceId(instanceId) >= 0) {
            continue;
        }
        openDeviceIndex(index);
    }
    upsertOpenPadsIntoCatalog();
}

void InputManager::openDeviceIndex(int index)
{
    OpenedPad pad;
    if (SDL_IsGameController(index)) {
        pad.controller = SDL_GameControllerOpen(index);
        if (!pad.controller) {
            qWarning().noquote() << QStringLiteral(
                "InputManager: SDL_GameControllerOpen failed index=%1 err=%2")
                                      .arg(index)
                                      .arg(QString::fromUtf8(SDL_GetError()));
            return;
        }
        pad.joystick = SDL_GameControllerGetJoystick(pad.controller);
    } else {
        pad.joystick = SDL_JoystickOpen(index);
        if (!pad.joystick) {
            qWarning().noquote() << QStringLiteral(
                "InputManager: SDL_JoystickOpen failed index=%1 err=%2")
                                      .arg(index)
                                      .arg(QString::fromUtf8(SDL_GetError()));
            return;
        }
    }

    pad.instanceId = SDL_JoystickInstanceID(pad.joystick);
    if (pad.instanceId >= 0 && padIndexByInstanceId(pad.instanceId) >= 0) {
        qInfo().noquote() << QStringLiteral(
            "InputManager: skip duplicate open index=%1 instance=%2")
                              .arg(index)
                              .arg(pad.instanceId);
        if (pad.controller) {
            SDL_GameControllerClose(pad.controller);
        } else if (pad.joystick) {
            SDL_JoystickClose(pad.joystick);
        }
        return;
    }

    pad.guid = joystickGuidString(pad.joystick);
    pad.instanceKey = makeInstanceKey(pad.guid, pad.instanceId);
    pad.name = pad.controller
        ? QString::fromUtf8(SDL_GameControllerName(pad.controller))
        : QString::fromUtf8(SDL_JoystickName(pad.joystick));
    pad.vendor = SDL_JoystickGetVendor(pad.joystick);
    pad.product = SDL_JoystickGetProduct(pad.joystick);
    const int sdlType = pad.controller ? static_cast<int>(SDL_GameControllerGetType(pad.controller)) : 0;
    const auto fromVid = GamepadCatalog::kindIdFromVidPid(pad.vendor, pad.product);
    const QString fromSdl = GamepadCatalog::kindIdFromSdlControllerType(sdlType);
    const QString fromName = GamepadCatalog::kindIdFromName(pad.name);
    const QString detectedKindId =
        GamepadCatalog::detectKindId(pad.vendor, pad.product, sdlType, pad.name);
    pad.detectedKind = kindFromKindId(detectedKindId);
    pad.virtualPad = GamepadCatalog::isSteamVirtualGamepad(pad.vendor, pad.product, pad.name);
    pad.builtin = GamepadCatalog::isBuiltinSteamDevice(pad.vendor, pad.product, pad.name);
    qInfo().noquote() << QStringLiteral(
        "InputManager: opened name=%1 key=%2 vidpid=%3 sdlType=%4 kindVid=%5 kindSdl=%6 kindName=%7 detected=%8")
                          .arg(pad.name,
                               pad.instanceKey,
                               formatVidPid(pad.vendor, pad.product),
                               QString::number(sdlType),
                               fromVid ? *fromVid : QStringLiteral("<none>"),
                               fromSdl,
                               fromName,
                               detectedKindId);
    qInfo().noquote() << QStringLiteral(
        "InputManager: opened flags name=%1 virtual=%2 builtin=%3 gc=%4")
                          .arg(pad.name,
                               pad.virtualPad ? QStringLiteral("yes") : QStringLiteral("no"),
                               pad.builtin ? QStringLiteral("yes") : QStringLiteral("no"),
                               pad.controller ? QStringLiteral("yes") : QStringLiteral("no"));
    m_pads.push_back(pad);
    seedPadTakeoverButtons(static_cast<int>(m_pads.size()) - 1);
}

void InputManager::dropSteamVirtualPadsIfRealPresent()
{
    bool hasReal = false;
    int virtualCount = 0;
    for (const OpenedPad &pad : m_pads) {
        if (pad.virtualPad) {
            ++virtualCount;
        } else {
            hasReal = true;
        }
    }
    if (!hasReal || virtualCount == 0) {
        return;
    }

    qInfo().noquote() << QStringLiteral(
        "InputManager: dropping steam virtual pads virtual=%1 realPresent=yes")
                          .arg(virtualCount);

    for (int index = static_cast<int>(m_pads.size()) - 1; index >= 0; --index) {
        if (!m_pads[static_cast<size_t>(index)].virtualPad) {
            continue;
        }
        const OpenedPad &pad = m_pads[static_cast<size_t>(index)];
        qInfo().noquote() << QStringLiteral("InputManager: drop virtual name=%1 key=%2")
                                .arg(pad.name, pad.instanceKey);
        const SDL_JoystickID instanceId = pad.instanceId;
        destroyPadHandles(index);
        m_pads.erase(m_pads.begin() + static_cast<size_t>(index));
        if (m_controllerInstanceId == instanceId) {
            m_controller = nullptr;
            m_joystick = nullptr;
            m_controllerInstanceId = -1;
        }
    }
}

void InputManager::bindExclusiveAliases()
{
    const int index = padIndexByInstanceKey(m_exclusiveInstanceKey);
    if (index < 0) {
        m_controller = nullptr;
        m_joystick = nullptr;
        m_controllerInstanceId = -1;
        return;
    }
    const OpenedPad &pad = m_pads[static_cast<size_t>(index)];
    m_controller = pad.controller;
    m_joystick = pad.joystick;
    m_controllerInstanceId = pad.instanceId;
}

void InputManager::assignExclusive(const QString &instanceKey, bool resetInputState)
{
    QString key = instanceKey;
    if (key.isEmpty() && !m_pads.empty()) {
        key = m_pads.front().instanceKey;
        qInfo().noquote() << QStringLiteral(
            "InputManager: exclusive fallback to first open pad key=%1")
                                .arg(key);
    }
    if (key.isEmpty()) {
        qInfo() << "InputManager: assignExclusive empty (no connected pads)";
        bindExclusiveAliases();
        updateConnectedGamepadInfo();
        persistGamepadCatalog();
        return;
    }

    const bool changed = m_exclusiveInstanceKey != key;
    if (changed) {
        const int newIndex = padIndexByInstanceKey(key);
        const QString newName = newIndex >= 0 ? m_pads[static_cast<size_t>(newIndex)].name
                                              : QStringLiteral("<not-open>");
        qInfo().noquote() << QStringLiteral(
            "InputManager: exclusive %1 -> %2 name=%3 resetInput=%4")
                              .arg(m_exclusiveInstanceKey.isEmpty() ? QStringLiteral("<none>")
                                                                    : m_exclusiveInstanceKey,
                                   key,
                                   newName,
                                   resetInputState ? QStringLiteral("yes") : QStringLiteral("no"));
    }
    m_exclusiveInstanceKey = key;
    bindExclusiveAliases();
    if (changed && resetInputState) {
        resetExclusiveInputState();
    }

    const int recordIndex = GamepadCatalog::findRecordIndex(m_knownGamepads, key);
    if (recordIndex >= 0) {
        m_knownGamepads[recordIndex].lastUsedMs = QDateTime::currentMSecsSinceEpoch();
        m_knownGamepads[recordIndex].instanceId = key;
    }

    updateConnectedGamepadInfo();
    persistGamepadCatalog();
}

void InputManager::maybeTakeOverFromOtherPads()
{
    const int exclusiveIndex = padIndexByInstanceKey(m_exclusiveInstanceKey);
    bool exclusiveButtonRising = false;
    if (exclusiveIndex >= 0) {
        OpenedPad &exclusive = m_pads[static_cast<size_t>(exclusiveIndex)];
        for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
            const bool pressed = padButtonPressed(exclusiveIndex, static_cast<SDL_GameControllerButton>(button));
            if (countsAsTakeoverButton(button) && pressed
                && !exclusive.takeoverButtonStates[static_cast<size_t>(button)]) {
                exclusiveButtonRising = true;
            }
        }
        const qreal exclusiveStick = padMaxStickMagnitude(exclusiveIndex);
        if (exclusiveStick < GamepadCatalog::kTakeoverStickThreshold) {
            exclusive.stickArmedForTakeover = true;
        }
    }

    QVector<int> order;
    for (int index = 0; index < static_cast<int>(m_pads.size()); ++index) {
        if (index == exclusiveIndex) {
            continue;
        }
        order.push_back(index);
    }
    std::sort(order.begin(), order.end(), [this](int left, int right) {
        const OpenedPad &leftPad = m_pads[static_cast<size_t>(left)];
        const OpenedPad &rightPad = m_pads[static_cast<size_t>(right)];
        if (leftPad.builtin != rightPad.builtin) {
            return !leftPad.builtin && rightPad.builtin;
        }
        return leftPad.instanceKey < rightPad.instanceKey;
    });

    for (int index : order) {
        OpenedPad &pad = m_pads[static_cast<size_t>(index)];
        bool buttonRising = false;
        int risingButton = -1;
        for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
            const bool pressed = padButtonPressed(index, static_cast<SDL_GameControllerButton>(button));
            if (countsAsTakeoverButton(button) && pressed
                && !pad.takeoverButtonStates[static_cast<size_t>(button)]) {
                buttonRising = true;
                if (risingButton < 0) {
                    risingButton = button;
                }
            }
            pad.takeoverButtonStates[static_cast<size_t>(button)] = pressed;
        }
        const qreal stickMag = padMaxStickMagnitude(index);
        const qreal stickForTakeover = pad.stickArmedForTakeover ? stickMag : 0.0;
        if (stickMag < GamepadCatalog::kTakeoverStickThreshold) {
            pad.stickArmedForTakeover = true;
        }
        if (GamepadCatalog::shouldTakeOverFromCandidate(buttonRising, stickForTakeover, exclusiveButtonRising)) {
            qInfo().noquote() << QStringLiteral(
                "InputManager: takeover from=%1 to=%2 name=%3 buttonRising=%4 stick=%5 risingButton=%6")
                                  .arg(m_exclusiveInstanceKey.isEmpty() ? QStringLiteral("<none>")
                                                                        : m_exclusiveInstanceKey,
                                       pad.instanceKey,
                                       pad.name,
                                       buttonRising ? QStringLiteral("yes") : QStringLiteral("no"),
                                       QString::number(stickMag, 'f', 3),
                                       QString::number(risingButton));
            assignExclusive(pad.instanceKey, true);
            return;
        }
        if (buttonRising) {
            qInfo().noquote() << QStringLiteral(
                "InputManager: takeover skipped name=%1 risingButton=%2 exclusiveRising=%3")
                                  .arg(pad.name,
                                       QString::number(risingButton),
                                       exclusiveButtonRising ? QStringLiteral("yes") : QStringLiteral("no"));
        }
    }

    if (exclusiveIndex >= 0) {
        OpenedPad &exclusive = m_pads[static_cast<size_t>(exclusiveIndex)];
        for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
            exclusive.takeoverButtonStates[static_cast<size_t>(button)] =
                padButtonPressed(exclusiveIndex, static_cast<SDL_GameControllerButton>(button));
        }
    }
}

void InputManager::seedPadTakeoverButtons(int padIndex)
{
    if (padIndex < 0 || padIndex >= static_cast<int>(m_pads.size())) {
        return;
    }
    OpenedPad &pad = m_pads[static_cast<size_t>(padIndex)];
    for (int button = 0; button < SDL_CONTROLLER_BUTTON_MAX; ++button) {
        pad.takeoverButtonStates[static_cast<size_t>(button)] =
            padButtonPressed(padIndex, static_cast<SDL_GameControllerButton>(button));
    }
    pad.stickArmedForTakeover =
        padMaxStickMagnitude(padIndex) < GamepadCatalog::kTakeoverStickThreshold;
}

void InputManager::resetExclusiveInputState()
{
    m_buttonStates.fill(false);
    m_gamepadButtonStates.fill(false);
    m_focusRepeatAtMs.fill(0);
    m_lastPollMs = 0;
    m_leftTriggerPressed = false;
    m_rightTriggerPressed = false;
    m_leftShoulderPressed = false;
    m_rightShoulderPressed = false;
    m_leftTriggerUsedForMotion = false;
    m_rightTriggerUsedForMotion = false;
    m_leftTriggerPressedAtMs = 0;
    m_rightTriggerPressedAtMs = 0;
    m_leftShoulderPressedAtMs = 0;
    m_rightShoulderPressedAtMs = 0;
    m_backPressedAtMs = 0;
    m_backLongPressHandled = false;
    m_leftShoulderChordUsed = false;
    m_rightShoulderChordUsed = false;
    setDragLockActive(false);
    m_anyStickWasActive = false;
    m_dpadRepeatAtMs.fill(0);
    setPreciseMode(false);
    setTurboMode(false);
}

Sint16 InputManager::padAxisValue(int padIndex, SDL_GameControllerAxis axis) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(m_pads.size())) {
        return 0;
    }
    const OpenedPad &pad = m_pads[static_cast<size_t>(padIndex)];
    if (pad.controller) {
        return SDL_GameControllerGetAxis(pad.controller, axis);
    }
    if (!pad.joystick) {
        return 0;
    }
    const int axisCount = SDL_JoystickNumAxes(pad.joystick);
    const auto readAxis = [&](int index) -> Sint16 {
        if (index < 0 || index >= axisCount) {
            return 0;
        }
        return SDL_JoystickGetAxis(pad.joystick, index);
    };
    switch (axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
        return readAxis(0);
    case SDL_CONTROLLER_AXIS_LEFTY:
        return readAxis(1);
    case SDL_CONTROLLER_AXIS_RIGHTX:
        return readAxis(2);
    case SDL_CONTROLLER_AXIS_RIGHTY:
        return readAxis(3);
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        return readAxis(4);
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        return readAxis(5);
    default:
        return 0;
    }
}

bool InputManager::padJoystickButtonPressed(int padIndex, int index) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(m_pads.size())) {
        return false;
    }
    SDL_Joystick *joystick = m_pads[static_cast<size_t>(padIndex)].joystick;
    if (!joystick) {
        return false;
    }
    const int buttonCount = SDL_JoystickNumButtons(joystick);
    if (index < 0 || index >= buttonCount) {
        return false;
    }
    return SDL_JoystickGetButton(joystick, index) != 0;
}

bool InputManager::padButtonPressed(int padIndex, SDL_GameControllerButton button) const
{
    if (padIndex < 0 || padIndex >= static_cast<int>(m_pads.size())) {
        return false;
    }
    const OpenedPad &pad = m_pads[static_cast<size_t>(padIndex)];
    if (pad.controller) {
        return SDL_GameControllerGetButton(pad.controller, button) != 0;
    }
    if (!pad.joystick) {
        return false;
    }
    const Uint8 hat = SDL_JoystickNumHats(pad.joystick) > 0 ? SDL_JoystickGetHat(pad.joystick, 0) : SDL_HAT_CENTERED;
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        return padJoystickButtonPressed(padIndex, 0);
    case SDL_CONTROLLER_BUTTON_B:
        return padJoystickButtonPressed(padIndex, 1);
    case SDL_CONTROLLER_BUTTON_X:
        return padJoystickButtonPressed(padIndex, 2);
    case SDL_CONTROLLER_BUTTON_Y:
        return padJoystickButtonPressed(padIndex, 3);
    case SDL_CONTROLLER_BUTTON_BACK:
        return joystickButtonPressedAny(pad.joystick, {8, 6, 9});
    case SDL_CONTROLLER_BUTTON_GUIDE:
        return joystickButtonPressedAny(pad.joystick, {10, 12});
    case SDL_CONTROLLER_BUTTON_START:
        return joystickButtonPressedAny(pad.joystick, {9, 7, 11});
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        return joystickButtonPressedAny(pad.joystick, {10, 13});
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        return joystickButtonPressedAny(pad.joystick, {11, 14});
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return padJoystickButtonPressed(padIndex, 4);
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return padJoystickButtonPressed(padIndex, 5);
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return (hat & SDL_HAT_UP) != 0 || padJoystickButtonPressed(padIndex, 12);
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return (hat & SDL_HAT_DOWN) != 0 || padJoystickButtonPressed(padIndex, 13);
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return (hat & SDL_HAT_LEFT) != 0 || padJoystickButtonPressed(padIndex, 14);
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return (hat & SDL_HAT_RIGHT) != 0 || padJoystickButtonPressed(padIndex, 15);
    default:
        return false;
    }
}

qreal InputManager::padMaxStickMagnitude(int padIndex) const
{
    const qreal leftX = std::abs(normalizeAxis(padAxisValue(padIndex, SDL_CONTROLLER_AXIS_LEFTX)));
    const qreal leftY = std::abs(normalizeAxis(padAxisValue(padIndex, SDL_CONTROLLER_AXIS_LEFTY)));
    const qreal rightX = std::abs(normalizeAxis(padAxisValue(padIndex, SDL_CONTROLLER_AXIS_RIGHTX)));
    const qreal rightY = std::abs(normalizeAxis(padAxisValue(padIndex, SDL_CONTROLLER_AXIS_RIGHTY)));
    return std::max(std::max(leftX, leftY), std::max(rightX, rightY));
}

bool InputManager::hasOpenController() const
{
    return m_controller || m_joystick;
}

Sint16 InputManager::axisValue(SDL_GameControllerAxis axis) const
{
    if (m_controller) {
        return SDL_GameControllerGetAxis(m_controller, axis);
    }

    if (!m_joystick) {
        return 0;
    }

    const int axisCount = SDL_JoystickNumAxes(m_joystick);
    const auto readAxis = [this, axisCount](int index) -> Sint16 {
        if (index < 0 || index >= axisCount) {
            return 0;
        }
        return SDL_JoystickGetAxis(m_joystick, index);
    };

    switch (axis) {
    case SDL_CONTROLLER_AXIS_LEFTX:
        return readAxis(0);
    case SDL_CONTROLLER_AXIS_LEFTY:
        return readAxis(1);
    case SDL_CONTROLLER_AXIS_RIGHTX:
        return readAxis(2);
    case SDL_CONTROLLER_AXIS_RIGHTY:
        return readAxis(3);
    case SDL_CONTROLLER_AXIS_TRIGGERLEFT:
        return readAxis(4);
    case SDL_CONTROLLER_AXIS_TRIGGERRIGHT:
        return readAxis(5);
    default:
        return 0;
    }
}

bool InputManager::joystickButtonPressed(int index) const
{
    if (!m_joystick) {
        return false;
    }

    const int buttonCount = SDL_JoystickNumButtons(m_joystick);
    if (index < 0 || index >= buttonCount) {
        return false;
    }

    return SDL_JoystickGetButton(m_joystick, index) != 0;
}

bool InputManager::buttonPressed(SDL_GameControllerButton button) const
{
    if (m_controller) {
        return SDL_GameControllerGetButton(m_controller, button) != 0;
    }

    if (!m_joystick) {
        return false;
    }

    const Uint8 hat = SDL_JoystickNumHats(m_joystick) > 0 ? SDL_JoystickGetHat(m_joystick, 0) : SDL_HAT_CENTERED;

    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        return joystickButtonPressed(0);
    case SDL_CONTROLLER_BUTTON_B:
        return joystickButtonPressed(1);
    case SDL_CONTROLLER_BUTTON_X:
        return joystickButtonPressed(2);
    case SDL_CONTROLLER_BUTTON_Y:
        return joystickButtonPressed(3);
    case SDL_CONTROLLER_BUTTON_BACK:
        return joystickButtonPressedAny(m_joystick, {8, 6, 9});
    case SDL_CONTROLLER_BUTTON_GUIDE:
        return joystickButtonPressedAny(m_joystick, {10, 12});
    case SDL_CONTROLLER_BUTTON_START:
        return joystickButtonPressedAny(m_joystick, {9, 7, 11});
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        return joystickButtonPressedAny(m_joystick, {10, 13});
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        return joystickButtonPressedAny(m_joystick, {11, 14});
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
        return joystickButtonPressed(4);
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
        return joystickButtonPressed(5);
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        return (hat & SDL_HAT_UP) != 0 || joystickButtonPressed(12);
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        return (hat & SDL_HAT_DOWN) != 0 || joystickButtonPressed(13);
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        return (hat & SDL_HAT_LEFT) != 0 || joystickButtonPressed(14);
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        return (hat & SDL_HAT_RIGHT) != 0 || joystickButtonPressed(15);
    default:
        return false;
    }
}

void InputManager::handleButtonPress(SDL_GameControllerButton button)
{
    if (m_suppressPageScopeInputUntilNextPoll) {
        return;
    }

    noteGamepadActivity();

    const bool leftShoulderHeld = m_leftShoulderPressed;
    const bool rightShoulderHeld = m_rightShoulderPressed;
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        if (m_dragLockActive) {
            setDragLockActive(false);
            break;
        }
        if (isUiFocusRoute()) {
            break;
        }
        if (shouldSuppressBrowserInjection()) {
            break;
        }
        if (leftShoulderHeld) {
            m_leftShoulderChordUsed = true;
            m_selectionInputSource = SelectionInputSource::Cursor;
            setDragLockActive(true, DragLockMode::PrimaryDrag);
            break;
        }
        if (rightShoulderHeld) {
            m_selectionInputSource = SelectionInputSource::Cursor;
            m_rightShoulderChordUsed = true;
            sendMouseClick(Qt::RightButton);
            break;
        }
        if (m_selectionInputSource == SelectionInputSource::Cursor) {
            sendMouseClick(Qt::LeftButton);
            break;
        }
        emit selectionClickRequested();
        emit browserClickRequested();
        break;
    case SDL_CONTROLLER_BUTTON_X:
        dispatchPageAction(QStringLiteral("closeTab"));
        break;
    case SDL_CONTROLLER_BUTTON_Y:
        dispatchPageAction(QStringLiteral("refresh"));
        break;
    case SDL_CONTROLLER_BUTTON_BACK:
        dispatchPageAction(QStringLiteral("toggleHud"));
        break;
    case SDL_CONTROLLER_BUTTON_START:
        dispatchPageAction(QStringLiteral("home"));
        break;
    case SDL_CONTROLLER_BUTTON_LEFTSTICK:
        dispatchPageAction(QStringLiteral("openHelp"));
        break;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
        break;
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        Q_UNUSED(leftShoulderHeld);
        dispatchDpadButton(button);
        break;
    default:
        break;
    }
}

void InputManager::handleDpadButton(qint64 nowMs, SDL_GameControllerButton button, int repeatIndex, bool pressed)
{
    const bool shoulderChordHeld = m_leftShoulderPressed || m_rightShoulderPressed;
    const bool wasPressed = m_buttonStates[button];

    if (pressed && !wasPressed) {
        dispatchDpadButton(button);
        m_dpadRepeatAtMs[repeatIndex] = nowMs + (shoulderChordHeld ? kDpadRepeatMs : kDpadInitialRepeatMs);
    } else if (pressed) {
        if (m_dpadRepeatAtMs[repeatIndex] > 0 && nowMs >= m_dpadRepeatAtMs[repeatIndex]) {
            dispatchDpadButton(button);
            m_dpadRepeatAtMs[repeatIndex] = nowMs + kDpadRepeatMs;
        }
    } else {
        m_dpadRepeatAtMs[repeatIndex] = 0;
    }

    m_buttonStates[button] = pressed;
}

void InputManager::dispatchLbDpadChordForHeldDirections()
{
    if (!m_leftShoulderPressed || m_leftShoulderChordUsed) {
        return;
    }

    if (buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_UP)) {
        dispatchDpadButton(SDL_CONTROLLER_BUTTON_DPAD_UP);
    } else if (buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_DOWN)) {
        dispatchDpadButton(SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    } else if (buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        dispatchDpadButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    } else if (buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        dispatchDpadButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    }
}

void InputManager::dispatchRbDpadChordForHeldDirections()
{
    Q_UNUSED(m_rightShoulderPressed);
    Q_UNUSED(m_rightShoulderChordUsed);
}

void InputManager::dispatchRtDpadChordForHeldDirections()
{
    Q_UNUSED(m_rightTriggerPressed);
}

void InputManager::dispatchLtDpadChordForHeldDirections()
{
    if (!m_leftTriggerPressed || m_leftTriggerUsedForMotion) {
        return;
    }

    if (buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_LEFT)) {
        m_leftTriggerUsedForMotion = true;
        dispatchDpadButton(SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    } else if (buttonPressed(SDL_CONTROLLER_BUTTON_DPAD_RIGHT)) {
        m_leftTriggerUsedForMotion = true;
        dispatchDpadButton(SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    }
}

void InputManager::dispatchDpadButton(SDL_GameControllerButton button)
{
    noteGamepadActivity();

    const bool leftShoulderHeld = m_leftShoulderPressed;
    const bool leftTriggerHeld = m_leftTriggerPressed;

    switch (button) {
    case SDL_CONTROLLER_BUTTON_DPAD_UP:
        if (leftShoulderHeld) {
            m_leftShoulderChordUsed = true;
            dispatchPageAction(QStringLiteral("zoomIn"));
            return;
        }
        m_selectionInputSource = SelectionInputSource::Dpad;
        dispatchNavigationKey(Qt::Key_Up);
        return;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
        if (leftShoulderHeld) {
            m_leftShoulderChordUsed = true;
            dispatchPageAction(QStringLiteral("zoomOut"));
            return;
        }
        m_selectionInputSource = SelectionInputSource::Dpad;
        dispatchNavigationKey(Qt::Key_Down);
        return;
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
        if (leftShoulderHeld) {
            m_leftShoulderChordUsed = true;
            dispatchPageAction(QStringLiteral("minFontIn"));
            return;
        }
        if (leftTriggerHeld) {
            m_leftTriggerUsedForMotion = true;
            dispatchPageAction(QStringLiteral("browserBack"));
            return;
        }
        m_selectionInputSource = SelectionInputSource::Dpad;
        dispatchNavigationKey(Qt::Key_Left);
        return;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
        if (leftShoulderHeld) {
            m_leftShoulderChordUsed = true;
            dispatchPageAction(QStringLiteral("minFontOut"));
            return;
        }
        if (leftTriggerHeld) {
            m_leftTriggerUsedForMotion = true;
            dispatchPageAction(QStringLiteral("browserForward"));
            return;
        }
        m_selectionInputSource = SelectionInputSource::Dpad;
        dispatchNavigationKey(Qt::Key_Right);
        return;
    default:
        return;
    }
}

void InputManager::handleCursorMotion(qreal deltaSeconds)
{
    if (!isBrowserPointerRoute()) {
        return;
    }

    if (m_leftAxisX == 0.0 && m_leftAxisY == 0.0) {
        return;
    }

    const qreal screenScale = cursorScreenSpeedScaleForGlobalPoint(m_cursorPosition.toPoint());
    const qreal speed = (m_turboMode ? kTurboCursorSpeed : (m_preciseMode ? kPreciseCursorSpeed : kCursorSpeed))
        * m_cursorSpeedScale * screenScale;
    m_cursorRemainder.rx() += m_leftAxisX * speed * deltaSeconds;
    m_cursorRemainder.ry() += m_leftAxisY * speed * deltaSeconds;

    const QPoint delta(qRound(m_cursorRemainder.x()), qRound(m_cursorRemainder.y()));
    if (delta.isNull()) {
        return;
    }

    m_selectionInputSource = SelectionInputSource::Cursor;
    m_cursorRemainder -= QPointF(delta);
    nudgeCursor(delta);
}

void InputManager::handleCursorScroll(qreal deltaSeconds)
{
    if (!isBrowserPointerRoute()) {
        return;
    }

    const QPoint pointerGlobal = m_cursorPosition.toPoint();
    const QPointF edgeVector = (m_browserFullscreenActive || !m_edgeScrollEnabled) ? QPointF() : edgeScrollVector();
    const qreal stickScrollX = qBound(-kEdgeScrollMaxMultiplier, m_rightAxisX, kEdgeScrollMaxMultiplier);
    const qreal stickScrollY = qBound(-kEdgeScrollMaxMultiplier, m_rightAxisY, kEdgeScrollMaxMultiplier);

    if (stickScrollX == 0.0 && stickScrollY == 0.0 && edgeVector.isNull()) {
        return;
    }

    const qreal screenScale = wheelScreenSpeedScaleForGlobalPoint(pointerGlobal);
    const qreal speed = (m_turboMode ? kTurboWheelSpeed : (m_preciseMode ? kPreciseWheelSpeed : kWheelSpeed))
        * m_wheelSpeedScale * screenScale;
    const qreal scrollMultiplier = kBrowserWheelMultiplier;

    if (!edgeVector.isNull()
        && m_activeBrowserView
        && !shouldSuppressBrowserInjection()
        && m_activeBrowserView->containsGlobalPoint(pointerGlobal)) {
        m_edgeScrollRemainder += edgeVector * speed * scrollMultiplier * deltaSeconds;
        const QPoint pixelDelta(qRound(m_edgeScrollRemainder.x()), qRound(m_edgeScrollRemainder.y()));
        if (!pixelDelta.isNull()) {
            m_edgeScrollRemainder -= QPointF(pixelDelta);
            m_activeBrowserView->syncPointerFromGlobal(pointerGlobal, m_heldMouseButtons);
            m_activeBrowserView->scrollByPixels(pixelDelta, pointerGlobal);
        }
    }

    if (stickScrollX == 0.0 && stickScrollY == 0.0) {
        return;
    }

    m_scrollRemainder.rx() += stickScrollX * speed * scrollMultiplier * deltaSeconds;
    m_scrollRemainder.ry() += stickScrollY * speed * scrollMultiplier * deltaSeconds;

    QPoint angleDelta(0, 0);
    while (m_scrollRemainder.x() >= 120.0) {
        angleDelta.rx() += 120;
        m_scrollRemainder.rx() -= 120.0;
    }
    while (m_scrollRemainder.x() <= -120.0) {
        angleDelta.rx() -= 120;
        m_scrollRemainder.rx() += 120.0;
    }
    while (m_scrollRemainder.y() >= 120.0) {
        angleDelta.ry() -= 120;
        m_scrollRemainder.ry() -= 120.0;
    }
    while (m_scrollRemainder.y() <= -120.0) {
        angleDelta.ry() += 120;
        m_scrollRemainder.ry() += 120.0;
    }

    if (!angleDelta.isNull()) {
        sendWheelStep(angleDelta);
    }
}

QPointF InputManager::edgeScrollVector() const
{
    QScreen *screen = screenForGlobalPoint(m_cursorPosition.toPoint());
    if (!screen) {
        return {};
    }

    const QRect bounds = screen->availableGeometry();
    if (bounds.isEmpty()) {
        return {};
    }

    auto edgeStrength = [](qreal distance) {
        if (distance >= kEdgeScrollMargin) {
            return 0.0;
        }

        const qreal proximity = qBound(0.0, 1.0 - distance / kEdgeScrollMargin, 1.0);
        return proximity * proximity * kEdgeScrollMaxMultiplier;
    };

    const qreal left = edgeStrength(m_cursorPosition.x() - bounds.left());
    const qreal right = edgeStrength(bounds.right() - m_cursorPosition.x());
    const qreal top = edgeStrength(m_cursorPosition.y() - bounds.top());
    const qreal bottom = edgeStrength(bounds.bottom() - m_cursorPosition.y());

    return QPointF(right - left, bottom - top);
}

void InputManager::handleFocusMotion(qint64 nowMs)
{
    const struct DirectionMapping {
        qreal axisValue;
        Qt::Key key;
        int index;
    } directions[] = {
        {m_leftAxisY < -kFocusThreshold ? -1.0 : 0.0, Qt::Key_Up, 0},
        {m_leftAxisY > kFocusThreshold ? 1.0 : 0.0, Qt::Key_Down, 1},
        {m_leftAxisX < -kFocusThreshold ? -1.0 : 0.0, Qt::Key_Left, 2},
        {m_leftAxisX > kFocusThreshold ? 1.0 : 0.0, Qt::Key_Right, 3},
    };

    for (const DirectionMapping &direction : directions) {
        if (direction.axisValue == 0.0) {
            m_focusRepeatAtMs[direction.index] = 0;
            continue;
        }

        if (m_focusRepeatAtMs[direction.index] == 0 || nowMs >= m_focusRepeatAtMs[direction.index]) {
            dispatchNavigationKey(direction.key);
            m_focusRepeatAtMs[direction.index] = nowMs + (m_focusRepeatAtMs[direction.index] == 0 ? kFocusInitialRepeatMs : kFocusRepeatMs);
        }
    }
}

void InputManager::sendKeyClick(int key)
{
    QObject *target = nullptr;
    if (isUiFocusRoute()) {
        target = QGuiApplication::focusObject();
    }
    if (!target) {
        target = keyTarget();
    }
    if (!target) {
        return;
    }

    QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyPress, key, Qt::NoModifier));
    QCoreApplication::postEvent(target, new QKeyEvent(QEvent::KeyRelease, key, Qt::NoModifier));
}

void InputManager::sendMouseMove(const QPoint &globalPos)
{
    if (!isBrowserPointerRoute()) {
        return;
    }

    updateCursorPosition(globalPos);
    const bool suppressBrowser = shouldSuppressBrowserInjection();

    if (m_dragLockActive && !suppressBrowser) {
        if (m_dragLockMode == DragLockMode::PrimaryDrag) {
            emit pointerTouchRequested(globalPos, PointerTouchEventType::Moved);
            emit browserTouchRequested(globalPos, BrowserTouchEventType::Moved);
        }
        return;
    }

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    setSystemCursorVisibleNative(true);
    m_systemCursorVisible = !useCustomCursorRendering();
    if (!m_dragLockActive && !suppressBrowser && sendSystemMouseMoveNative(globalPos)) {
        emit cursorSyncRequested();
        emit browserCursorSyncRequested();
        return;
    }
#endif

    if (m_activeBrowserView && !suppressBrowser) {
        m_activeBrowserView->syncPointerFromGlobal(globalPos, m_heldMouseButtons);
    } else if (QWidget *widget = mouseTargetWidget(globalPos)) {
        const QPoint local = widget->mapFromGlobal(globalPos);
        QMouseEvent event(
            QEvent::MouseMove,
            QPointF(local),
            QPointF(widget->window()->mapFromGlobal(globalPos)),
            QPointF(globalPos),
            Qt::NoButton,
            m_heldMouseButtons,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(widget, &event);
    } else if (QWindow *window = targetWindow()) {
        const QPoint local = window->mapFromGlobal(globalPos);
        QMouseEvent event(
            QEvent::MouseMove,
            QPointF(local),
            QPointF(local),
            QPointF(globalPos),
            Qt::NoButton,
            m_heldMouseButtons,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(window, &event);
    }

    emit cursorSyncRequested();
    emit browserCursorSyncRequested();
}

void InputManager::sendMouseButtonEvent(Qt::MouseButton button, bool pressed)
{
    if (button == Qt::NoButton || !isBrowserPointerRoute()) {
        return;
    }

    const QPoint global = m_cursorPosition.toPoint();
    updateCursorPosition(global);

    const Qt::MouseButtons buttons = pressed ? (m_heldMouseButtons | button) : (m_heldMouseButtons & ~button);
    m_heldMouseButtons = buttons;

    if (m_modalOverlayActive) {
        return;
    }

    if (!focusWidgetWithinDialogLikeOverlay()) {
        emit pointerButtonRequested(global, button, pressed, buttons);
        emit browserMouseButtonRequested(global, button, pressed, buttons);
        emit cursorSyncRequested();
        emit browserCursorSyncRequested();
        return;
    }

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    if (sendSystemMouseButtonNative(global, button, pressed)) {
        emit cursorSyncRequested();
        emit browserCursorSyncRequested();
        return;
    }
#endif

    if (QWidget *widget = mouseTargetWidget(global)) {
        const QPoint local = widget->mapFromGlobal(global);
        QMouseEvent event(
            pressed ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
            QPointF(local),
            QPointF(widget->window()->mapFromGlobal(global)),
            QPointF(global),
            button,
            buttons,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(widget, &event);
    } else if (QWindow *window = targetWindow()) {
        const QPoint local = window->mapFromGlobal(global);
        QMouseEvent event(
            pressed ? QEvent::MouseButtonPress : QEvent::MouseButtonRelease,
            QPointF(local),
            QPointF(local),
            QPointF(global),
            button,
            buttons,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(window, &event);
    }

    emit cursorSyncRequested();
    emit browserCursorSyncRequested();
}

void InputManager::setDragLockActive(bool active, DragLockMode mode)
{
    if (m_dragLockActive == active) {
        return;
    }

    m_dragLockActive = active;
    m_dragLockMode = active ? mode : DragLockMode::Inactive;
    emit dragLockActiveChanged();

    if (isBrowserPointerRoute() && !shouldSuppressBrowserInjection()) {
        const PointerTouchEventType eventType = active ? PointerTouchEventType::Pressed
                                                       : PointerTouchEventType::Released;
        if (m_dragLockMode == DragLockMode::PrimaryDrag || (!active && mode == DragLockMode::PrimaryDrag)) {
            emit pointerTouchRequested(m_cursorPosition.toPoint(), eventType);
            emit browserTouchRequested(m_cursorPosition.toPoint(), static_cast<BrowserTouchEventType>(eventType));
        }
    }

    emit cursorSyncRequested();
    emit browserCursorSyncRequested();
}

void InputManager::sendMouseClick(Qt::MouseButton button)
{
    if (!isBrowserPointerRoute()) {
        return;
    }

    if (shouldSuppressBrowserInjection()) {
        return;
    }

    const QPoint global = m_cursorPosition.toPoint();
    updateCursorPosition(global);
    const bool dialogOverlayActive = focusWidgetWithinDialogLikeOverlay();

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    if (sendSystemMouseClickNative(global, button)) {
        return;
    }
#endif

    if (m_activeBrowserView && !dialogOverlayActive) {
        m_activeBrowserView->sendMouseButtonFromGlobal(global, button, false, button);
        m_activeBrowserView->sendMouseButtonFromGlobal(global, button, true, Qt::NoButton);
    } else if (QWidget *widget = mouseTargetWidget(global)) {
        const QPoint local = widget->mapFromGlobal(global);
        QMouseEvent pressEvent(
            QEvent::MouseButtonPress,
            QPointF(local),
            QPointF(widget->window()->mapFromGlobal(global)),
            QPointF(global),
            button,
            button,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(widget, &pressEvent);

        QMouseEvent releaseEvent(
            QEvent::MouseButtonRelease,
            QPointF(local),
            QPointF(widget->window()->mapFromGlobal(global)),
            QPointF(global),
            button,
            Qt::NoButton,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(widget, &releaseEvent);
    } else if (QWindow *window = targetWindow()) {
        const QPoint local = window->mapFromGlobal(global);
        QMouseEvent pressEvent(
            QEvent::MouseButtonPress,
            QPointF(local),
            QPointF(local),
            QPointF(global),
            button,
            button,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(window, &pressEvent);

        QMouseEvent releaseEvent(
            QEvent::MouseButtonRelease,
            QPointF(local),
            QPointF(local),
            QPointF(global),
            button,
            Qt::NoButton,
            Qt::NoModifier,
            QPointingDevice::primaryPointingDevice());
        QCoreApplication::sendEvent(window, &releaseEvent);
    }
}

void InputManager::sendWheelStep(const QPoint &angleDelta)
{
    if (!isBrowserPointerRoute()) {
        return;
    }

    const QPoint global = m_cursorPosition.toPoint();
    const bool suppressBrowser = shouldSuppressBrowserInjection();
    if (!suppressBrowser) {
        emit wheelStepRequested(global, angleDelta);
    }

    if (m_activeBrowserView && !suppressBrowser) {
        // BrowserContainer already handles wheelStepRequested for the active view;
        return;
    }

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    if (!suppressBrowser && sendSystemMouseWheelNative(global, angleDelta)) {
        return;
    }
#endif

    if (QWidget *widget = mouseTargetWidget(global)) {
        const QPoint local = widget->mapFromGlobal(global);
        QCoreApplication::postEvent(widget, new QWheelEvent(
            QPointF(local),
            QPointF(global),
            QPoint(),
            angleDelta,
            Qt::NoButton,
            Qt::NoModifier,
            Qt::ScrollUpdate,
            false,
            Qt::MouseEventNotSynthesized,
            QPointingDevice::primaryPointingDevice()));
    } else if (QWindow *window = targetWindow()) {
        const QPoint local = window->mapFromGlobal(global);
        QCoreApplication::postEvent(window, new QWheelEvent(
            QPointF(local),
            QPointF(global),
            QPoint(),
            angleDelta,
            Qt::NoButton,
            Qt::NoModifier,
            Qt::ScrollUpdate,
            false,
            Qt::MouseEventNotSynthesized,
            QPointingDevice::primaryPointingDevice()));
    }
}

void InputManager::nudgeCursor(const QPoint &delta)
{
    if (!isBrowserPointerRoute()) {
        return;
    }

    QPoint global = m_cursorPosition.toPoint() + delta;
    QScreen *screen = screenForGlobalPoint(global);
    if (!screen) {
        return;
    }

    const QRect bounds = screen->availableGeometry();
    global.setX(qBound(bounds.left(), global.x(), bounds.right()));
    global.setY(qBound(bounds.top(), global.y(), bounds.bottom()));

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
    if (!m_dragLockActive) {
        setSystemCursorVisibleNative(true);
        m_systemCursorVisible = !useCustomCursorRendering();
    }
    if (!m_dragLockActive && sendSystemMouseMoveNative(global)) {
        updateCursorPosition(global);
        emit cursorSyncRequested();
        emit browserCursorSyncRequested();
        return;
    }
#endif

    if (!m_dragLockActive) {
        QCursor::setPos(global);
    }
    sendMouseMove(global);
}

void InputManager::updateCursorPosition(const QPointF &globalPos, bool emitChanges)
{
    const bool changed = m_cursorPosition != globalPos;
    m_cursorPosition = globalPos;
    if (!emitChanges) {
        return;
    }

    emit pointerActivity();
    if (changed) {
        emit cursorPositionChanged();
    }
}

void InputManager::sendTouchMouseEvent(QWindow *window, QEvent::Type type, const QPointF &globalPos, Qt::MouseButton button, Qt::MouseButtons buttons)
{
    const QPointF localPos = QPointF(window->mapFromGlobal(globalPos.toPoint()));
    QMouseEvent event(
        type,
        localPos,
        localPos,
        globalPos,
        button,
        buttons,
        Qt::NoModifier,
        QPointingDevice::primaryPointingDevice());
    QCoreApplication::sendEvent(window, &event);
}

void InputManager::sendTouchToActiveBrowser(QEvent::Type type, const QPointF &globalPos)
{
    if (!m_activeBrowserView) {
        return;
    }

    const QPoint global = globalPos.toPoint();
    switch (type) {
    case QEvent::MouseButtonPress:
        m_activeBrowserView->beginTouchDragFromGlobal(global);
        break;
    case QEvent::MouseMove:
        m_activeBrowserView->updateTouchDragFromGlobal(global);
        break;
    case QEvent::MouseButtonRelease:
        m_activeBrowserView->endTouchDragFromGlobal(global);
        break;
    default:
        break;
    }
}