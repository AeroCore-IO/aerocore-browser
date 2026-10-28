#include "inputmanageradapter.h"

InputManagerAdapter::InputManagerAdapter(InputManager *manager, QObject *parent)
    : QObject(parent)
    , m_manager(manager)
{
    connectSignals();
}

void InputManagerAdapter::connectSignals()
{
    connect(m_manager, &InputManager::cursorPositionChanged,
            this, &InputManagerAdapter::cursorPositionChanged);
    connect(m_manager, &InputManager::navigationModeChanged,
            this, &InputManagerAdapter::navigationModeChanged);
    connect(m_manager, &InputManager::preciseModeChanged,
            this, &InputManagerAdapter::preciseModeChanged);
    connect(m_manager, &InputManager::turboModeChanged,
            this, &InputManagerAdapter::turboModeChanged);
    connect(m_manager, &InputManager::inputRouteChanged, this, [this]() {
        emit inputRouteChanged();
        emit homeNavigationEnabledChanged();
    });
    connect(m_manager, &InputManager::modalOverlayActiveChanged,
            this, &InputManagerAdapter::modalOverlayActiveChanged);
    connect(m_manager, &InputManager::dragLockActiveChanged,
            this, &InputManagerAdapter::dragLockActiveChanged);
    connect(m_manager, &InputManager::activeInputDeviceChanged,
            this, &InputManagerAdapter::activeInputDeviceChanged);
    connect(m_manager, &InputManager::connectedGamepadChanged,
            this, &InputManagerAdapter::connectedGamepadChanged);
    connect(m_manager, &InputManager::inputDeviceNameChanged,
            this, &InputManagerAdapter::inputDeviceNameChanged);
    connect(m_manager, &InputManager::textInputActiveChanged,
            this, &InputManagerAdapter::textInputActiveChanged);

    connect(m_manager, &InputManager::gamepadButtonPressed,
            this, &InputManagerAdapter::gamepadButtonPressed);
    connect(m_manager, &InputManager::gamepadButtonReleased,
            this, &InputManagerAdapter::gamepadButtonReleased);
    connect(m_manager, &InputManager::gamepadButtonLongPressed,
            this, &InputManagerAdapter::gamepadButtonLongPressed);

    connect(m_manager, &InputManager::actionTriggered,
            this, &InputManagerAdapter::actionTriggered);
    connect(m_manager, &InputManager::pointerActivity,
            this, &InputManagerAdapter::pointerActivity);

    connect(m_manager, &InputManager::selectionClickRequested, this, [this]() {
        emit selectionClickRequested();
        emit browserClickRequested();
    });

    connect(m_manager, &InputManager::cursorSyncRequested, this, [this]() {
        emit cursorSyncRequested();
        emit browserCursorSyncRequested();
    });

    connect(m_manager, &InputManager::navigationKeyRequested,
            this, &InputManagerAdapter::navigationKeyRequested);
    connect(m_manager, &InputManager::browserNavigationKeyRequested,
            this, &InputManagerAdapter::browserNavigationKeyRequested);

    connect(m_manager, &InputManager::pointerButtonRequested,
            this, [this](const QPoint &globalPos, Qt::MouseButton button,
                   bool pressed, Qt::MouseButtons buttons) {
                emit browserMouseButtonRequested(globalPos.x(), globalPos.y(),
                                                 static_cast<int>(button), pressed,
                                                 static_cast<int>(buttons));
            });

    connect(m_manager, &InputManager::pointerTouchRequested,
            this, [this](const QPoint &globalPos, InputManager::PointerTouchEventType eventType) {
                emit browserTouchRequested(globalPos.x(), globalPos.y(),
                                           static_cast<int>(eventType));
            });

    connect(m_manager, &InputManager::wheelStepRequested,
            this, [this](const QPoint &globalPos, const QPoint &angleDelta) {
                emit wheelStepRequested(globalPos.x(), globalPos.y(),
                                        angleDelta.x(), angleDelta.y());
            });
}
