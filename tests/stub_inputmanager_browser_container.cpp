#include "inputmanager.h"

#include "browserview.h"

InputManager::InputManager(QObject *parent)
    : QObject(parent)
{
}

InputManager::~InputManager() = default;

QPointF InputManager::cursorPosition() const
{
    return {};
}

QString InputManager::navigationMode() const
{
    return QStringLiteral("cursor");
}

bool InputManager::preciseMode() const
{
    return false;
}

bool InputManager::turboMode() const
{
    return false;
}

InputManager::SelectionInputSource InputManager::selectionInputSource() const
{
    return SelectionInputSource::Cursor;
}

InputManager::InputRoute InputManager::inputRoute() const
{
    return m_inputRoute;
}

void InputManager::setInputRoute(InputRoute route)
{
    m_inputRoute = route;
}

bool InputManager::modalOverlayActive() const
{
    return false;
}

void InputManager::setModalOverlayActive(bool active)
{
    Q_UNUSED(active);
}

InputManager::InputDeviceKind InputManager::activeInputDevice() const
{
    return InputDeviceKind::Unknown;
}

InputManager::InputDeviceKind InputManager::connectedGamepadKind() const
{
    return InputDeviceKind::Unknown;
}

QString InputManager::inputDeviceName() const
{
    return {};
}

QString InputManager::resolvedButtonIconLayout() const
{
    return QStringLiteral("xbox");
}

bool InputManager::homeNavigationEnabled() const
{
    return m_inputRoute == InputRoute::UiFocus;
}

bool InputManager::dragLockActive() const
{
    return false;
}

Qt::MouseButtons InputManager::heldMouseButtons() const
{
    return m_heldMouseButtons;
}

void InputManager::triggerAction(const QString &action)
{
    Q_UNUSED(action);
}

void InputManager::toggleNavigationMode()
{
}

void InputManager::setPreciseMode(bool enabled)
{
    Q_UNUSED(enabled);
}

void InputManager::setTurboMode(bool enabled)
{
    Q_UNUSED(enabled);
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
    m_cursorSpeedScale = qBound(0.5, scale, 2.0);
}

void InputManager::setWheelSpeedScale(qreal scale)
{
    m_wheelSpeedScale = qBound(0.5, scale, 2.0);
}

void InputManager::setEdgeScrollEnabled(bool enabled)
{
    m_edgeScrollEnabled = enabled;
}

void InputManager::setHomeNavigationEnabled(bool enabled)
{
    setInputRoute(enabled ? InputRoute::UiFocus : InputRoute::BrowserPointer);
}

void InputManager::consumeBackButton()
{
}

void InputManager::nudgeCursorByKeyboard(const QPoint &delta)
{
    Q_UNUSED(delta);
}

void InputManager::setBrowserFullscreenActive(bool active)
{
    Q_UNUSED(active);
}

void InputManager::setActiveBrowserView(BrowserView *view)
{
    Q_UNUSED(view);
}

BrowserView *InputManager::activeBrowserView() const
{
    return nullptr;
}

void InputManager::setSystemCursorVisible(bool visible)
{
    Q_UNUSED(visible);
}

void InputManager::refreshSystemCursor()
{
}

void InputManager::openSteamKeyboard()
{
}

void InputManager::closeSteamKeyboard()
{
}

void InputManager::openWebKeyboardSession(BrowserView *view)
{
    Q_UNUSED(view);
}

void InputManager::closeWebKeyboardSession()
{
}

void InputManager::acknowledgeSteamKeyboardClosed()
{
}

void InputManager::setTextInputActive(bool active)
{
    Q_UNUSED(active);
}

bool InputManager::textInputActive() const
{
    return false;
}

QString InputManager::activeInputDeviceKindId() const
{
    return QStringLiteral("none");
}

QString InputManager::connectedGamepadKindId() const
{
    return QStringLiteral("none");
}

QString InputManager::activeInputDeviceDisplayName() const
{
    return {};
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
    case InputDeviceKind::GamepadXbox:
        return QStringLiteral("xbox");
    case InputDeviceKind::GamepadPlayStation:
        return QStringLiteral("playstation");
    case InputDeviceKind::GamepadNintendo:
        return QStringLiteral("nintendo");
    case InputDeviceKind::GamepadSteam:
        return QStringLiteral("steam");
    case InputDeviceKind::GamepadGeneric:
        return QStringLiteral("gamepad");
    case InputDeviceKind::Unknown:
        break;
    }
    return QStringLiteral("none");
}

InputManager::InputDeviceKind InputManager::gamepadKindFromDeviceName(const QString &name)
{
    Q_UNUSED(name);
    return InputDeviceKind::Unknown;
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
    return {};
}

bool InputManager::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    Q_UNUSED(event);
    return false;
}
