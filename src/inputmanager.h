#pragma once

#include <QEvent>
#include <QObject>
#include <QPointer>
#include <QPointF>
#include <QString>
#include <QTimer>
#include <Qt>
#include <QVector>
#include <QWindow>

#include <SDL.h>

#include <array>
#include <vector>

#include "gamepadcatalog.h"

class BrowserView;
class QWidget;

// Routes gamepad, keyboard, and touchscreen input to the Qt UI or browser view.
//
// Input scopes, matching InputManagerAdapter / InputTypes.InputRoute:
//   UiFocus (system scope) emits raw gamepadButton* signals to QML; polling does not emit actionTriggered.
//   BrowserPointer (page scope) also emits semantic actions and injects CEF pointer, wheel, and navigation events.
class InputManager : public QObject
{
    Q_OBJECT

    // Global screen coordinates of the virtual cursor; updated by the stick only in BrowserPointer mode.
    Q_PROPERTY(QPointF cursorPosition READ cursorPosition NOTIFY cursorPositionChanged)
    Q_PROPERTY(QString navigationMode READ navigationMode NOTIFY navigationModeChanged)
    Q_PROPERTY(bool preciseMode READ preciseMode WRITE setPreciseMode NOTIFY preciseModeChanged)
    Q_PROPERTY(bool turboMode READ turboMode WRITE setTurboMode NOTIFY turboModeChanged)
    // Base cursor speed multiplier (0.5–2.0), multiplied by precise/turbo mode factors.
    Q_PROPERTY(qreal cursorSpeedScale READ cursorSpeedScale WRITE setCursorSpeedScale NOTIFY cursorSpeedScaleChanged)
    // Base wheel speed multiplier (0.5–2.0), multiplied by precise/turbo mode factors.
    Q_PROPERTY(qreal wheelSpeedScale READ wheelSpeedScale WRITE setWheelSpeedScale NOTIFY wheelSpeedScaleChanged)
    // Whether to scroll the page automatically when the cursor nears a screen edge.
    Q_PROPERTY(bool edgeScrollEnabled READ edgeScrollEnabled WRITE setEdgeScrollEnabled NOTIFY edgeScrollEnabledChanged)
    // InputRoute enum value: UiFocus or BrowserPointer.
    Q_PROPERTY(int inputRoute READ inputRouteValue WRITE setInputRouteValue NOTIFY inputRouteChanged)
    // Whether a modal overlay such as MenuPanel is active.
    Q_PROPERTY(bool modalOverlayActive READ modalOverlayActive WRITE setModalOverlayActive NOTIFY modalOverlayActiveChanged)
    // InputDeviceKind value of the most recent input source.
    Q_PROPERTY(int activeInputDevice READ activeInputDeviceValue NOTIFY activeInputDeviceChanged)
    // InputDeviceKind value of the connected exclusive gamepad, or Unknown if none.
    Q_PROPERTY(int connectedGamepadKind READ connectedGamepadKindValue NOTIFY connectedGamepadChanged)
    // SDL name of the exclusive gamepad, or empty if none.
    Q_PROPERTY(QString inputDeviceName READ inputDeviceName NOTIFY inputDeviceNameChanged)
    // Current button icon layout; retains the last layout when no gamepad is connected.
    Q_PROPERTY(QString resolvedButtonIconLayout READ resolvedButtonIconLayout NOTIFY resolvedButtonIconLayoutChanged)
    // Whether a Steam on-screen keyboard text input session is active.
    Q_PROPERTY(bool textInputActive READ textInputActive NOTIFY textInputActiveChanged)

public:
    explicit InputManager(QObject *parent = nullptr);
    ~InputManager() override;

    // Input source used to confirm/select.
    enum class SelectionInputSource {
        Cursor, // Cursor mode: A simulates a left mouse click.
        Dpad    // D-pad mode: A emits selectionClickRequested for QML focus selection.
    };

    // Input route (input scope).
    enum class InputRoute {
        UiFocus,        // System scope: shell UI consumes raw gamepadButton* names in QML.
        BrowserPointer  // Page scope: CEF browsing consumes actionTriggered and browser* signals.
    };

    // Input device type.
    enum class InputDeviceKind {
        Unknown,           // No device or unknown type.
        Mouse,
        Keyboard,
        TouchScreen,
        GamepadGeneric,    // Generic gamepad without a recognized mapping.
        GamepadXbox,
        GamepadPlayStation,
        GamepadNintendo,
        GamepadSteam,
    };

    // Pointer touch event type, used by features such as drag lock.
    enum class PointerTouchEventType {
        Pressed,
        Moved,
        Released
    };

    // Legacy alias.
    using BrowserTouchEventType = PointerTouchEventType;

    // Physical gamepad buttons; gamepadButton* signals carry the string returned by gamepadButtonName().
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
    static constexpr int GamepadButtonCount = static_cast<int>(GamepadButton::Guide) + 1;

    // Returns the virtual cursor's global position.
    QPointF cursorPosition() const;
    // Returns the current navigation mode.
    QString navigationMode() const;
    bool preciseMode() const;
    bool turboMode() const;
    // Returns the base cursor and wheel speed multipliers.
    qreal cursorSpeedScale() const;
    qreal wheelSpeedScale() const;
    // Returns whether edge scrolling is enabled.
    bool edgeScrollEnabled() const;
    // Returns the input source used for selection (cursor or D-pad).
    SelectionInputSource selectionInputSource() const;

    // Returns the current input route.
    InputRoute inputRoute() const;
    // Route changes consume unfinished button edges to prevent a B press started in UiFocus
    // from dispatching back when released after switching to BrowserPointer.
    void setInputRoute(InputRoute route);
    // Integer accessors for Q_PROPERTY.
    int inputRouteValue() const { return static_cast<int>(inputRoute()); }
    void setInputRouteValue(int route) { setInputRoute(static_cast<InputRoute>(route)); }

    // Returns or sets whether a modal overlay is active.
    bool modalOverlayActive() const;
    void setModalOverlayActive(bool active);

    // Returns the most recent input device and connected gamepad type.
    InputDeviceKind activeInputDevice() const;
    InputDeviceKind connectedGamepadKind() const;
    // Integer accessors for Q_PROPERTY.
    int activeInputDeviceValue() const { return static_cast<int>(activeInputDevice()); }
    int connectedGamepadKindValue() const { return static_cast<int>(connectedGamepadKind()); }
    // Returns the SDL name of the exclusive gamepad, or empty if none.
    QString inputDeviceName() const;
    // Returns the current button icon layout ID; retains the last result when disconnected.
    QString resolvedButtonIconLayout() const;

    // Returns whether UI focus navigation is enabled (inputRoute() == UiFocus).
    bool homeNavigationEnabled() const;
    // Returns whether drag lock is active (LB+A toggles it; the left stick then emits touch events).
    bool dragLockActive() const;
    // Returns the currently held mouse buttons.
    Qt::MouseButtons heldMouseButtons() const;

    // Triggers a named action; actionTriggered is emitted only in BrowserPointer (page scope).
    // Common actions: home, refresh, history, address, back, previousTab, nextTab,
    // previousSession, nextSession, closeSession, zoomIn, zoomOut, minFontIn, minFontOut,
    // browserBack, browserForward, toggleHud, togglePageOverlay.
    Q_INVOKABLE void triggerAction(const QString &action);
    // Toggles navigation mode; currently a no-op retained for QML compatibility.
    Q_INVOKABLE void toggleNavigationMode();
    Q_INVOKABLE void setPreciseMode(bool enabled);
    Q_INVOKABLE void setTurboMode(bool enabled);
    // Sets the base cursor/wheel speed multipliers, clamped to 0.5–2.0.
    Q_INVOKABLE void setCursorSpeedScale(qreal scale);
    Q_INVOKABLE void setWheelSpeedScale(qreal scale);
    // Sets whether edge scrolling is enabled.
    Q_INVOKABLE void setEdgeScrollEnabled(bool enabled);
    // Enables/disables UI focus navigation (equivalent to setInputRoute(UiFocus/BrowserPointer)).
    Q_INVOKABLE void setHomeNavigationEnabled(bool enabled);
    // Consumes the held B button so its long-press closeSession and release back actions are not
    // dispatched after switching from system scope to the browser page.
    Q_INVOKABLE void consumeBackButton();
    // Nudges the virtual cursor by pixel offset; used by keyboard input in BrowserPointer mode.
    Q_INVOKABLE void nudgeCursorByKeyboard(const QPoint &delta);
    // Notifies the manager of browser fullscreen state; leaving fullscreen clears drag lock and edge scrolling.
    void setBrowserFullscreenActive(bool active);
    void setActiveBrowserView(BrowserView *view);
    BrowserView *activeBrowserView() const;
    // Shows or hides the system cursor; it remains hidden when rendering a custom cursor.
    Q_INVOKABLE void setSystemCursorVisible(bool visible);
    // Refreshes system cursor state and syncs it with the virtual cursor; ignored in UiFocus mode.
    Q_INVOKABLE void refreshSystemCursor();
    // Opens the Steam on-screen keyboard (Mode=0); supported in Steam Deck/Game Mode sessions.
    Q_INVOKABLE void openSteamKeyboard();
    // Explicitly closes the Steam keyboard as a fallback; normally it closes itself and is acknowledged.
    Q_INVOKABLE void closeSteamKeyboard();
    // Opens a text-input session when a web editable element gains focus, even if the keyboard
    // deeplink failed or the user opened the OSK manually with Steam+X.
    void openWebKeyboardSession(BrowserView *view);
    // Ends the session when the web editable element loses focus.
    void closeWebKeyboardSession();
    // Syncs local state after the Steam OSK closes itself without sending steam://close/keyboard.
    Q_INVOKABLE void acknowledgeSteamKeyboardClosed();
    // Explicitly sets text-input state; false is equivalent to closeSteamKeyboard().
    Q_INVOKABLE void setTextInputActive(bool active);
    bool textInputActive() const;

    // Returns stable string IDs for the active or connected input device.
    Q_INVOKABLE QString activeInputDeviceKindId() const;
    Q_INVOKABLE QString connectedGamepadKindId() const;
    // Returns a localized display name for the active input device.
    Q_INVOKABLE QString activeInputDeviceDisplayName() const;
    // Converts InputDeviceKind to a stable string ID.
    Q_INVOKABLE static QString inputDeviceKindIdFor(InputDeviceKind kind);
    // Detects the gamepad type from its SDL name, preferring Steam Controller/Deck over Xbox keywords.
    static InputDeviceKind gamepadKindFromDeviceName(const QString &name);
    // Converts GamepadButton to its name (for example, "A", "LB", or "DpadUp").
    Q_INVOKABLE static QString gamepadButtonName(GamepadButton button);

signals:
    // Raw button press edge in both scopes; buttonName comes from gamepadButtonName().
    void gamepadButtonPressed(const QString &buttonName);
    // Raw button release edge in both scopes.
    void gamepadButtonReleased(const QString &buttonName);
    // Raw long-press edge in both scopes (currently B after 650 ms).
    void gamepadButtonLongPressed(const QString &buttonName);
    // Semantic action emitted only in BrowserPointer (page scope).
    void actionTriggered(const QString &action);
    // A button pressed in D-pad mode, emitted by polling only in BrowserPointer.
    void selectionClickRequested();
    // Requests that QML sync the custom cursor after its position changes.
    void cursorSyncRequested();
    // Requests browser mouse-button injection with global position, button, state, and held buttons.
    void pointerButtonRequested(const QPoint &globalPos, Qt::MouseButton button, bool pressed, Qt::MouseButtons buttons);
    // Requests browser touch-event injection for drag lock.
    void pointerTouchRequested(const QPoint &globalPos, PointerTouchEventType eventType);
    // Requests directional navigation using Qt::Key_* values; also posts a key event to the focused item.
    void navigationKeyRequested(int key);
    // Requests browser wheel-event injection with global position and angle delta.
    void wheelStepRequested(const QPoint &globalPos, const QPoint &angleDelta);
    // Emitted on pointer activity such as cursor movement or clicks; can reset idle timers.
    void pointerActivity();
    void cursorPositionChanged();
    void navigationModeChanged();
    void preciseModeChanged();
    void turboModeChanged();
    void cursorSpeedScaleChanged();
    void wheelSpeedScaleChanged();
    void edgeScrollEnabledChanged();
    void inputRouteChanged();
    void modalOverlayActiveChanged();
    void activeInputDeviceChanged();
    void connectedGamepadChanged();
    void inputDeviceNameChanged();
    void resolvedButtonIconLayoutChanged();
    void dragLockActiveChanged();
    void selectionInputSourceChanged();
    void textInputActiveChanged();

    // Legacy browser* signals, emitted alongside the current signals during migration.
    void browserClickRequested();
    void browserCursorSyncRequested();
    void browserMouseButtonRequested(const QPoint &globalPos, Qt::MouseButton button, bool pressed, Qt::MouseButtons buttons);
    void browserTouchRequested(const QPoint &globalPos, BrowserTouchEventType eventType);
    void browserNavigationKeyRequested(int key);

protected:
    // Tracks mouse/touch activity and syncs the cursor in BrowserPointer mode.
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    enum class DragLockMode {
        Inactive,
        PrimaryDrag
    };

    bool isUiFocusRoute() const;
    bool isBrowserPointerRoute() const;
    // Blocks CEF pointer/touch injection while a QML modal or dialog-like overlay is active.
    bool shouldSuppressBrowserInjection() const;
    bool routeNavigationThroughSignal() const;
    void dispatchNavigationKey(int key);
    void dispatchPageAction(const QString &action);
    // Clears button timers and unconsumed flags so release edges do not dispatch page actions.
    void consumeInFlightPageActionEdges();
    void emitGamepadButtonEdge(GamepadButton button, bool pressed);
    void updateGamepadButtonEdges(bool emitSignals,
                                  bool actionPressed,
                                  bool backPressed,
                                  bool leftShoulderPressed,
                                  bool rightShoulderPressed,
                                  bool leftTriggerPressed,
                                  bool rightTriggerPressed);

    void pollGamepad();
    void applySteamInputBypassHints();
    void loadGamepadCatalog();
    void persistGamepadCatalog() const;
    void openMissingPads();
    void openDeviceIndex(int index);
    void closePadByInstanceId(SDL_JoystickID instanceId);
    void closeAllPads();
    void destroyPadHandles(int padIndex);
    void dropSteamVirtualPadsIfRealPresent();
    void bindExclusiveAliases();
    void assignExclusive(const QString &instanceKey, bool resetInputState);
    void maybeTakeOverFromOtherPads();
    void resetExclusiveInputState();
    void updateConnectedGamepadInfo();
    void upsertOpenPadsIntoCatalog();
    QVector<GamepadCatalog::DeviceRecord> connectedCatalogRecords() const;
    QVector<QString> connectedInstanceIds() const;
    int padIndexByInstanceId(SDL_JoystickID instanceId) const;
    int padIndexByInstanceKey(const QString &instanceKey) const;
    QString makeInstanceKey(const QString &guid, SDL_JoystickID instanceId) const;
    static QString joystickGuidString(SDL_Joystick *joystick);
    Sint16 padAxisValue(int padIndex, SDL_GameControllerAxis axis) const;
    bool padButtonPressed(int padIndex, SDL_GameControllerButton button) const;
    bool padJoystickButtonPressed(int padIndex, int index) const;
    qreal padMaxStickMagnitude(int padIndex) const;
    void seedPadTakeoverButtons(int padIndex);
    InputDeviceKind kindFromKindId(const QString &kindId) const;
    void logSdlJoystickList(const char *reason) const;
    void logGamepadInventory(const char *reason) const;
    void setActiveInputDevice(InputDeviceKind kind);
    void noteGamepadActivity();
    bool hasOpenController() const;
    Sint16 axisValue(SDL_GameControllerAxis axis) const;
    bool joystickButtonPressed(int index) const;
    bool buttonPressed(SDL_GameControllerButton button) const;
    void handleButtonPress(SDL_GameControllerButton button);
    void handleDpadButton(qint64 nowMs, SDL_GameControllerButton button, int repeatIndex, bool pressed);
    void dispatchDpadButton(SDL_GameControllerButton button);
    void dispatchLbDpadChordForHeldDirections();
    void dispatchRbDpadChordForHeldDirections();
    void dispatchLtDpadChordForHeldDirections();
    void dispatchRtDpadChordForHeldDirections();
    void handleCursorMotion(qreal deltaSeconds);
    void handleCursorScroll(qreal deltaSeconds);
    QPointF edgeScrollVector() const;
    void handleFocusMotion(qint64 nowMs);
    void sendKeyClick(int key);
    void sendMouseMove(const QPoint &globalPos);
    void sendMouseButtonEvent(Qt::MouseButton button, bool pressed);
    void setDragLockActive(bool active, DragLockMode mode = DragLockMode::Inactive);
    void sendMouseClick(Qt::MouseButton button);
    void sendWheelStep(const QPoint &angleDelta);
    void nudgeCursor(const QPoint &delta);
    void updateCursorPosition(const QPointF &globalPos, bool emitChanges = true);
    void sendTouchMouseEvent(QWindow *window, QEvent::Type type, const QPointF &globalPos, Qt::MouseButton button, Qt::MouseButtons buttons);
    void sendTouchToActiveBrowser(QEvent::Type type, const QPointF &globalPos);
    void initializeCursorAtScreenCenter();
    bool isSteamKeyboardAvailable() const;
    // Whether a session blocks gamepad/navigation input: shell sessions always block; web sessions
    // block only while the keyboard is open so gamepad navigation remains available in focused fields.
    bool oskBlockingSession() const;
    // Restores window focus after a delay when the OSK closes.
    void scheduleWindowReactivateAfterOsk();
    void syncTextInputActiveProperty();
    void captureTextInputTarget();
    void clearTextInputTarget();
    bool redirectTextInputEvent(QObject *watched, QEvent *event);
    // Syncs the local session after the Steam OSK closes itself without a close deeplink.
    void maybeAcknowledgeSteamKeyboardClosed(const char *reason);
    void installSteamKeyboardDismissWatchers();
    void noteTextInputActivity();
    bool shouldAcceptAppendChunk(const QString &chunk, qint64 nowMs);

    // m_buttonStates tracks SDL buttons for semantic press/hold/chord edges.
    // m_gamepadButtonStates tracks logical GamepadButton edges for raw gamepadButton* signals.
    friend class InputManagerTestAccess;

    struct OpenedPad {
        SDL_GameController *controller = nullptr;
        SDL_Joystick *joystick = nullptr;
        SDL_JoystickID instanceId = -1;
        QString instanceKey;
        QString guid;
        QString name;
        quint16 vendor = 0;
        quint16 product = 0;
        InputDeviceKind detectedKind = InputDeviceKind::GamepadGeneric;
        bool builtin = false;
        bool virtualPad = false;
        bool stickArmedForTakeover = false;
        std::array<bool, SDL_CONTROLLER_BUTTON_MAX> takeoverButtonStates{};
    };

    QPointF m_cursorPosition;
    QPointF m_lastTouchPosition;
    QString m_navigationMode = QStringLiteral("cursor");
    QString m_inputDeviceName;
    QString m_resolvedButtonIconLayout = QStringLiteral("xbox");
    QString m_exclusiveInstanceKey;
    SelectionInputSource m_selectionInputSource = SelectionInputSource::Dpad;
    InputRoute m_inputRoute = InputRoute::UiFocus;
    InputDeviceKind m_activeInputDevice = InputDeviceKind::Unknown;
    InputDeviceKind m_connectedGamepadKind = InputDeviceKind::Unknown;
    bool m_modalOverlayActive = false;
    bool m_preciseMode = false;
    bool m_turboMode = false;
    qreal m_cursorSpeedScale = 1.0;
    qreal m_wheelSpeedScale = 1.0;
    bool m_edgeScrollEnabled = true;
    bool m_touchMousePressed = false;
    bool m_leftTriggerPressed = false;
    bool m_rightTriggerPressed = false;
    bool m_leftShoulderPressed = false;
    bool m_rightShoulderPressed = false;
    bool m_leftTriggerUsedForMotion = false;
    bool m_rightTriggerUsedForMotion = false;
    bool m_backLongPressHandled = false;
    bool m_backConsumed = false;
    // Pending B-consumption flag: QML may call consumeBackButton() synchronously from the press
    // signal before m_buttonStates is updated, so the edge handler confirms it later.
    bool m_backConsumePending = false;
    bool m_leftShoulderChordUsed = false;
    bool m_rightShoulderChordUsed = false;
    bool m_dragLockActive = false;
    DragLockMode m_dragLockMode = DragLockMode::Inactive;
    Qt::MouseButtons m_heldMouseButtons = Qt::NoButton;
    qint64 m_leftTriggerPressedAtMs = 0;
    qint64 m_rightTriggerPressedAtMs = 0;
    qint64 m_leftShoulderPressedAtMs = 0;
    qint64 m_rightShoulderPressedAtMs = 0;
    qint64 m_backPressedAtMs = 0;
    QTimer m_pollTimer;
    // Window-focus retry timer after the OSK closes.
    QTimer *m_windowReactivateTimer = nullptr;
    int m_windowReactivateAttempts = 0;
    // Gamepad input recovery window after the OSK closes; appState gating is ignored during this window.
    qint64 m_oskInputRecoveryUntilMs = 0;
    bool m_controllerInputWasGated = false;
    // Non-owning aliases for the exclusive gamepad; the actual handles live in m_pads.
    SDL_GameController *m_controller = nullptr;
    SDL_Joystick *m_joystick = nullptr;
    SDL_JoystickID m_controllerInstanceId = -1;
    std::vector<OpenedPad> m_pads;
    QVector<GamepadCatalog::DeviceRecord> m_knownGamepads;
    std::array<bool, SDL_CONTROLLER_BUTTON_MAX> m_buttonStates{};
    std::array<bool, GamepadButtonCount> m_gamepadButtonStates{};
    std::array<qint64, 4> m_dpadRepeatAtMs{};
    std::array<qint64, 4> m_focusRepeatAtMs{};
    QPointF m_cursorRemainder;
    QPointF m_edgeScrollRemainder;
    QPointF m_scrollRemainder;
    qreal m_leftAxisX = 0.0;
    qreal m_leftAxisY = 0.0;
    qreal m_rightAxisX = 0.0;
    qreal m_rightAxisY = 0.0;
    qint64 m_lastPollMs = 0;
    bool m_anyStickWasActive = false;
    bool m_browserFullscreenActive = false;
    QPointer<BrowserView> m_activeBrowserView;
    bool m_systemCursorVisible = true;
    bool m_textInputActive = false;
    bool m_textInputBackWasPressed = false;
    bool m_steamKeyboardOpen = false;
    QPointer<QObject> m_textInputTarget;
    qint64 m_textInputGraceUntilMs = 0;
    qint64 m_steamKeyboardOpenedAtMs = 0;
    qint64 m_lastTextInputAtMs = 0;
    qint64 m_lastAppendAtMs = 0;
    QString m_lastAppendChunk;
    bool m_steamKeyboardSawInactive = false;
    bool m_webTextInputSession = false;
    qint64 m_webKeyboardClosedAtMs = 0;
    bool m_leftStickWasActive = false;
    // If QML changes the route synchronously during polling, suppress remaining page-scope input
    // for this poll to avoid dispatching the same A/R3 edge after switching to BrowserPointer.
    bool m_suppressPageScopeInputUntilNextPoll = false;
};

Q_DECLARE_METATYPE(InputManager::InputRoute)
Q_DECLARE_METATYPE(InputManager::InputDeviceKind)
Q_DECLARE_METATYPE(InputManager::PointerTouchEventType)
Q_DECLARE_METATYPE(InputManager::GamepadButton)
