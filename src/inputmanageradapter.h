#pragma once

#include <QObject>
#include <QPointF>
#include "inputmanager.h"

// InputManagerAdapter exposes the input system to QML as the cppInputManager context property.
//
// The inputRoute property selects one of two scopes:
//
// system scope (InputRoute.UiFocus): shell UI pages interpret raw gamepad button signals through
// the focused item's FocusInterface. Semantic actionTriggered signals are not emitted.
//
// page scope (InputRoute.BrowserPointer): the browser page uses InputManager's fixed action mapping
// and receives CEF pointer/navigation injection signals. Raw button signals remain available.
//
// MainWindow changes the scope during page transitions.
// Signal consumption by scope:
//   gamepadButtonPressed/Released/LongPressed — system: primary | page: optional (hints)
//   actionTriggered                           — system: none    | page: primary
//   selectionClickRequested                   — system: none    | page: some cases
//   navigationKeyRequested                    — system: Qt      | page: Qt + CEF
//   browserNavigationKeyRequested and browser*— system: none    | page: CEF injection
//   cursorPositionChanged                     — system: none    | page: virtual cursor tracking

namespace InputTypes {
Q_NAMESPACE

enum class TouchEventType {
    Pressed,
    Moved,
    Released
};
Q_ENUM_NS(TouchEventType)

enum class SelectionSource {
    Cursor,
    Dpad
};
Q_ENUM_NS(SelectionSource)

// Input route/scope, readable and writable from QML through cppInputManager.inputRoute.
enum class InputRoute {
    UiFocus = 0,
    BrowserPointer = 1
};
Q_ENUM_NS(InputRoute)

enum class InputDeviceKind {
    Unknown,
    Mouse,
    Keyboard,
    TouchScreen,
    GamepadGeneric,
    GamepadXbox,
    GamepadPlayStation,
    GamepadNintendo,
    GamepadSteam
};
Q_ENUM_NS(InputDeviceKind)

// Physical gamepad buttons. gamepadButton* signals carry the string returned by gamepadButtonName().
enum class GamepadButton {
    A,
    B,
    X,
    Y,
    Back,
    Start,
    LB,
    RB,
    LT,
    RT,
    L3,
    R3,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
    Guide
};
Q_ENUM_NS(GamepadButton)

static_assert(static_cast<int>(GamepadButton::Guide) + 1 == InputManager::GamepadButtonCount,
              "InputTypes::GamepadButton must stay aligned with InputManager::GamepadButton");
}

// Forwards InputManager signals to QML, gated by inputRoute; see the scope description above.
class InputManagerAdapter : public QObject
{
    Q_OBJECT

    Q_PROPERTY(QPointF cursorPosition READ cursorPosition NOTIFY cursorPositionChanged)
    Q_PROPERTY(QString navigationMode READ navigationMode NOTIFY navigationModeChanged)
    Q_PROPERTY(bool preciseMode READ preciseMode WRITE setPreciseMode NOTIFY preciseModeChanged)
    Q_PROPERTY(bool turboMode READ turboMode WRITE setTurboMode NOTIFY turboModeChanged)
    Q_PROPERTY(int inputRoute READ inputRoute WRITE setInputRoute NOTIFY inputRouteChanged)
    Q_PROPERTY(bool modalOverlayActive READ modalOverlayActive WRITE setModalOverlayActive NOTIFY modalOverlayActiveChanged)
    Q_PROPERTY(bool homeNavigationEnabled READ homeNavigationEnabled
                   WRITE setHomeNavigationEnabled NOTIFY homeNavigationEnabledChanged)
    Q_PROPERTY(bool dragLockActive READ dragLockActive NOTIFY dragLockActiveChanged)
    Q_PROPERTY(int selectionInputSource READ selectionInputSource NOTIFY selectionInputSourceChanged)
    Q_PROPERTY(int heldMouseButtons READ heldMouseButtons NOTIFY heldMouseButtonsChanged)
    Q_PROPERTY(int activeInputDevice READ activeInputDevice NOTIFY activeInputDeviceChanged)
    Q_PROPERTY(int connectedGamepadKind READ connectedGamepadKind NOTIFY connectedGamepadChanged)
    Q_PROPERTY(QString inputDeviceName READ inputDeviceName NOTIFY inputDeviceNameChanged)
    Q_PROPERTY(bool textInputActive READ textInputActive NOTIFY textInputActiveChanged)

public:
    explicit InputManagerAdapter(InputManager *manager, QObject *parent = nullptr);

    QPointF cursorPosition() const { return m_manager->cursorPosition(); }
    QString navigationMode() const { return m_manager->navigationMode(); }
    bool preciseMode() const { return m_manager->preciseMode(); }
    bool turboMode() const { return m_manager->turboMode(); }
    int inputRoute() const { return static_cast<int>(m_manager->inputRoute()); }
    bool modalOverlayActive() const { return m_manager->modalOverlayActive(); }
    bool homeNavigationEnabled() const { return m_manager->homeNavigationEnabled(); }
    bool dragLockActive() const { return m_manager->dragLockActive(); }
    int selectionInputSource() const { return static_cast<int>(m_manager->selectionInputSource()); }
    int heldMouseButtons() const { return static_cast<int>(m_manager->heldMouseButtons()); }
    int activeInputDevice() const { return static_cast<int>(m_manager->activeInputDevice()); }
    int connectedGamepadKind() const { return static_cast<int>(m_manager->connectedGamepadKind()); }
    QString inputDeviceName() const { return m_manager->inputDeviceName(); }
    bool textInputActive() const { return m_manager->textInputActive(); }

    void setPreciseMode(bool enabled) { m_manager->setPreciseMode(enabled); }
    void setTurboMode(bool enabled) { m_manager->setTurboMode(enabled); }
    void setInputRoute(int route) { m_manager->setInputRoute(static_cast<InputManager::InputRoute>(route)); }
    void setModalOverlayActive(bool active) { m_manager->setModalOverlayActive(active); }
    void setHomeNavigationEnabled(bool enabled) { m_manager->setHomeNavigationEnabled(enabled); }

    // Triggers a semantic action; actionTriggered is emitted only in BrowserPointer (page scope).
    Q_INVOKABLE void triggerAction(const QString &action) { m_manager->triggerAction(action); }
    // Consumes the held B button so switching pages or opening a dialog in system scope
    // does not trigger browser back/closeSession when B is released.
    Q_INVOKABLE void consumeBackButton() { m_manager->consumeBackButton(); }
    Q_INVOKABLE void nudgeCursor(qreal dx, qreal dy) {
        m_manager->nudgeCursorByKeyboard(QPoint(static_cast<int>(dx), static_cast<int>(dy)));
    }
    Q_INVOKABLE void toggleNavigationMode() { m_manager->toggleNavigationMode(); }
    Q_INVOKABLE void setSystemCursorVisible(bool visible) { m_manager->setSystemCursorVisible(visible); }
    Q_INVOKABLE void refreshSystemCursor() { m_manager->refreshSystemCursor(); }
    Q_INVOKABLE QString activeInputDeviceKindId() const { return m_manager->activeInputDeviceKindId(); }
    Q_INVOKABLE QString connectedGamepadKindId() const { return m_manager->connectedGamepadKindId(); }
    Q_INVOKABLE QString activeInputDeviceDisplayName() const { return m_manager->activeInputDeviceDisplayName(); }
    Q_INVOKABLE void openSteamKeyboard() { m_manager->openSteamKeyboard(); }
    Q_INVOKABLE void closeSteamKeyboard() { m_manager->closeSteamKeyboard(); }
    Q_INVOKABLE void acknowledgeSteamKeyboardClosed() { m_manager->acknowledgeSteamKeyboardClosed(); }
    Q_INVOKABLE void setTextInputActive(bool active) { m_manager->setTextInputActive(active); }
    // Converts a GamepadButton value to a name for QML comparisons and logging.
    Q_INVOKABLE static QString gamepadButtonName(int button) {
        return InputManager::gamepadButtonName(static_cast<InputManager::GamepadButton>(button));
    }

signals:
    // --- Route and state (both scopes) ---
    void cursorPositionChanged();
    void navigationModeChanged();
    void preciseModeChanged();
    void turboModeChanged();
    void inputRouteChanged();
    void modalOverlayActiveChanged();
    void homeNavigationEnabledChanged();
    void dragLockActiveChanged();
    void selectionInputSourceChanged();
    void heldMouseButtonsChanged();
    void activeInputDeviceChanged();
    void connectedGamepadChanged();
    void inputDeviceNameChanged();
    void textInputActiveChanged();

    // --- system scope entry point: raw buttons (preferred by QML in UiFocus) ---
    // buttonName is a stable string: "A", "B", "X", "Y", "Back", "Start", "LB", "RB",
    // "LT", "RT", "L3", "R3", "DpadUp", "DpadDown", "DpadLeft", "DpadRight", "Guide"
    void gamepadButtonPressed(const QString &buttonName);
    void gamepadButtonReleased(const QString &buttonName);
    void gamepadButtonLongPressed(const QString &buttonName);

    void pointerActivity();

    // --- page scope: semantic actions (emitted by InputManager only in BrowserPointer) ---
    void actionTriggered(const QString &action);

    // --- Shared navigation signals (selectionClickRequested is not emitted in UiFocus) ---
    void selectionClickRequested();
    void cursorSyncRequested();
    void navigationKeyRequested(int key);
    void wheelStepRequested(qreal x, qreal y, int deltaX, int deltaY);

    // --- page scope: browser pointer injection (BrowserPointer only) ---
    void browserClickRequested();
    void browserCursorSyncRequested();
    void browserMouseButtonRequested(qreal x, qreal y, int button, bool pressed, int buttons);
    void browserTouchRequested(qreal x, qreal y, int eventType);
    void browserNavigationKeyRequested(int key);

private:
    InputManager *m_manager;
    void connectSignals();
};
