#include "browserinpututils.h"
#include "browserview.h"
#include "browserview_test_access.h"
#include "gamepadtrigger.h"
#include "inputmanager.h"
#include "inputmanageradapter.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDateTime>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QScreen>
#include <QSignalSpy>
#include <QTest>
#include <QWidget>

#include <SDL.h>

#include <cstdint>

class InputManagerTestAccess
{
public:
    static void dispatchNavigationKey(InputManager *manager, int key)
    {
        manager->dispatchNavigationKey(key);
    }

    static void dispatchDpadButton(InputManager *manager, SDL_GameControllerButton button)
    {
        manager->dispatchDpadButton(button);
    }

    static void setLeftShoulderPressed(InputManager *manager, bool pressed)
    {
        manager->m_leftShoulderPressed = pressed;
    }

    static void setRightShoulderPressed(InputManager *manager, bool pressed)
    {
        manager->m_rightShoulderPressed = pressed;
    }

    static void setLeftTriggerPressed(InputManager *manager, bool pressed)
    {
        manager->m_leftTriggerPressed = pressed;
    }

    static void armHeldBackPress(InputManager *manager, qint64 pressedAtMs = 1)
    {
        manager->m_buttonStates[SDL_CONTROLLER_BUTTON_B] = true;
        manager->m_backPressedAtMs = pressedAtMs;
        manager->m_backLongPressHandled = false;
    }

    static void simulateBackReleaseEdge(InputManager *manager)
    {
        if (!manager->m_buttonStates[SDL_CONTROLLER_BUTTON_B]) {
            return;
        }
        if (!manager->m_backLongPressHandled) {
            manager->dispatchPageAction(QStringLiteral("back"));
        }
        manager->m_backPressedAtMs = 0;
        manager->m_backLongPressHandled = false;
        manager->m_buttonStates[SDL_CONTROLLER_BUTTON_B] = false;
    }

    static void armHeldLeftShoulderShortPress(InputManager *manager, qint64 pressedAtMs = 1)
    {
        manager->m_buttonStates[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = true;
        manager->m_leftShoulderPressed = true;
        manager->m_leftShoulderPressedAtMs = pressedAtMs;
        manager->m_leftShoulderChordUsed = false;
    }

    static void simulateLeftShoulderReleaseEdge(InputManager *manager, qint64 nowMs)
    {
        if (!manager->m_buttonStates[SDL_CONTROLLER_BUTTON_LEFTSHOULDER]) {
            return;
        }
        if (!manager->m_leftShoulderChordUsed
            && manager->m_leftShoulderPressedAtMs > 0
            && nowMs - manager->m_leftShoulderPressedAtMs <= 360) {
            manager->dispatchPageAction(QStringLiteral("previousTab"));
        }
        manager->m_leftShoulderPressedAtMs = 0;
        manager->m_leftShoulderChordUsed = false;
        manager->m_leftShoulderPressed = false;
        manager->m_buttonStates[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = false;
    }

    static void armHeldLeftTriggerShortPress(InputManager *manager, qint64 pressedAtMs = 1)
    {
        manager->m_leftTriggerPressed = true;
        manager->m_leftTriggerPressedAtMs = pressedAtMs;
        manager->m_leftTriggerUsedForMotion = false;
    }

    static void simulateLeftTriggerReleaseEdge(InputManager *manager, qint64 nowMs)
    {
        if (!manager->m_leftTriggerPressed) {
            return;
        }
        if (!manager->m_leftTriggerUsedForMotion
            && manager->m_leftTriggerPressedAtMs > 0
            && nowMs - manager->m_leftTriggerPressedAtMs <= 360) {
            manager->dispatchPageAction(QStringLiteral("previousSession"));
        }
        manager->m_leftTriggerPressedAtMs = 0;
        manager->m_leftTriggerUsedForMotion = false;
        manager->m_leftTriggerPressed = false;
    }

    static void handleButtonPress(InputManager *manager, SDL_GameControllerButton button)
    {
        manager->handleButtonPress(button);
    }

    static bool pageScopeInputSuppressed(const InputManager *manager)
    {
        return manager->m_suppressPageScopeInputUntilNextPoll;
    }

    static void clearPageScopeInputSuppression(InputManager *manager)
    {
        manager->m_suppressPageScopeInputUntilNextPoll = false;
    }

    static void sendMouseButtonEvent(InputManager *manager, Qt::MouseButton button, bool pressed)
    {
        manager->sendMouseButtonEvent(button, pressed);
    }

    static bool redirectTextInputEvent(InputManager *manager, QObject *watched, QEvent *event)
    {
        return manager->redirectTextInputEvent(watched, event);
    }

    static void armTextInputGrace(InputManager *manager, qint64 graceUntilMs)
    {
        manager->m_textInputActive = false;
        manager->m_steamKeyboardOpen = false;
        manager->m_textInputTarget.clear();
        manager->m_textInputGraceUntilMs = graceUntilMs;
        manager->syncTextInputActiveProperty();
    }

    static void setTextInputTarget(InputManager *manager, QObject *target)
    {
        manager->m_textInputTarget = target;
    }

    static void setTextInputSession(InputManager *manager, bool active)
    {
        manager->m_textInputActive = active;
        manager->m_steamKeyboardOpen = active;
        if (!active) {
            manager->m_textInputGraceUntilMs = 0;
        }
        manager->syncTextInputActiveProperty();
    }
};

class TestInputManager : public QObject
{
    Q_OBJECT

private slots:
    void gamepadButtonNameReturnsStableIds();
    void gamepadButtonCountMatchesEnumSize();
    void triggerHysteresisSuppressesChatterDoubleRelease();
    void triggerActionRespectsInputRoute();
    void enteringBrowserPointerInitializesCursor();
    void routeSwitchConsumesInFlightBackRelease();
    void routeSwitchConsumesInFlightShoulderRelease();
    void routeSwitchConsumesInFlightTriggerRelease();
    void routeSwitchSuppressesSamePollButtonPress();
    void dispatchNavigationKeyRespectsBrowserNavigationRoute();
    void adapterForwardsBrowserNavigationOnlyFromManager();
    void dispatchDpadButtonLbChordDispatchesPageActions();
    void dispatchDpadButtonLtChordDispatchesPageActions();
    void dispatchDpadButtonRbChordFallsBackToNavigation();
    void browserKeyMappingCoversEditingAndFunctionKeys();
    void browserKeyModifiersPreserveShortcuts();
    void dialogOverlayDoesNotClickThroughToBrowser();
    void gracePeriodDoesNotSwallowNavigationKeys();
    void textInputSessionSwallowsNavigationKeys();
    void enterDuringTextSessionAcknowledgesKeyboardClosed();
    void duplicateCjkKeyPressIsDeduped();
    void steamAndDeckNamesMapToSteamKind();
    void virtualPadAndXboxNamesDoNotMapToSteam();

private:
    InputManager m_manager;
};

void TestInputManager::gamepadButtonNameReturnsStableIds()
{
    QCOMPARE(InputManager::gamepadButtonName(InputManager::GamepadButton::A), QStringLiteral("A"));
    QCOMPARE(InputManager::gamepadButtonName(InputManager::GamepadButton::B), QStringLiteral("B"));
    QCOMPARE(InputManager::gamepadButtonName(InputManager::GamepadButton::LB), QStringLiteral("LB"));
    QCOMPARE(InputManager::gamepadButtonName(InputManager::GamepadButton::DpadUp), QStringLiteral("DpadUp"));
    QCOMPARE(InputManager::gamepadButtonName(InputManager::GamepadButton::Guide), QStringLiteral("Guide"));
    QCOMPARE(InputManagerAdapter::gamepadButtonName(static_cast<int>(InputTypes::GamepadButton::B)),
             QStringLiteral("B"));
}

void TestInputManager::gamepadButtonCountMatchesEnumSize()
{
    QCOMPARE(InputManager::GamepadButtonCount, 17);
    QCOMPARE(static_cast<int>(InputManager::GamepadButton::Guide) + 1, InputManager::GamepadButtonCount);
    QCOMPARE(static_cast<int>(InputTypes::GamepadButton::Guide) + 1, InputManager::GamepadButtonCount);
}

void TestInputManager::triggerHysteresisSuppressesChatterDoubleRelease()
{
    const std::int16_t samples[] = {0, 20000, 14000, 18000, 5000, 0};
    const auto singleThreshold = [](std::int16_t axis) {
        return axis > GamepadTrigger::kPressThreshold;
    };

    bool heldSingle = false;
    bool heldHysteresis = false;
    int fallingSingle = 0;
    int fallingHysteresis = 0;
    for (const std::int16_t sample : samples) {
        const bool nextSingle = singleThreshold(sample);
        if (heldSingle && !nextSingle) {
            ++fallingSingle;
        }
        heldSingle = nextSingle;

        const bool nextHysteresis = GamepadTrigger::isHeld(sample, heldHysteresis);
        if (heldHysteresis && !nextHysteresis) {
            ++fallingHysteresis;
        }
        heldHysteresis = nextHysteresis;
    }

    QVERIFY2(fallingSingle >= 2, "fixture must reproduce single-threshold double-fire");
    QCOMPARE(fallingHysteresis, 1);
}

void TestInputManager::triggerActionRespectsInputRoute()
{
    QSignalSpy actionSpy(&m_manager, &InputManager::actionTriggered);

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    m_manager.triggerAction(QStringLiteral("back"));
    QCOMPARE(actionSpy.count(), 0);

    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    m_manager.triggerAction(QStringLiteral("back"));
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.at(0).at(0).toString(), QStringLiteral("back"));
}

void TestInputManager::enteringBrowserPointerInitializesCursor()
{
    QSignalSpy cursorSpy(&m_manager, &InputManager::cursorPositionChanged);
    QSignalSpy syncSpy(&m_manager, &InputManager::cursorSyncRequested);

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    cursorSpy.clear();
    syncSpy.clear();

    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);

    // Regression: initializeCursorAtScreenCenter used to run before m_inputRoute
    // flipped to BrowserPointer, so it early-returned and page scope had no overlay.
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        const QPoint center = screen->availableGeometry().center();
        QCOMPARE(m_manager.cursorPosition().toPoint(), center);
        QVERIFY(cursorSpy.count() >= 1 || syncSpy.count() >= 1);
    } else {
        QSKIP("No primary screen available for cursor-center assertion");
    }
}

void TestInputManager::routeSwitchConsumesInFlightBackRelease()
{
    QSignalSpy actionSpy(&m_manager, &InputManager::actionTriggered);

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    InputManagerTestAccess::armHeldBackPress(&m_manager);

    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::simulateBackReleaseEdge(&m_manager);
    QCOMPARE(actionSpy.count(), 0);

    InputManagerTestAccess::clearPageScopeInputSuppression(&m_manager);
    InputManagerTestAccess::armHeldBackPress(&m_manager);
    InputManagerTestAccess::simulateBackReleaseEdge(&m_manager);
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.at(0).at(0).toString(), QStringLiteral("back"));
}

void TestInputManager::routeSwitchConsumesInFlightShoulderRelease()
{
    QSignalSpy actionSpy(&m_manager, &InputManager::actionTriggered);

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    InputManagerTestAccess::armHeldLeftShoulderShortPress(&m_manager);

    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::simulateLeftShoulderReleaseEdge(&m_manager, 100);
    QCOMPARE(actionSpy.count(), 0);
}

void TestInputManager::routeSwitchConsumesInFlightTriggerRelease()
{
    QSignalSpy actionSpy(&m_manager, &InputManager::actionTriggered);

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    InputManagerTestAccess::armHeldLeftTriggerShortPress(&m_manager);

    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::simulateLeftTriggerReleaseEdge(&m_manager, 100);
    QCOMPARE(actionSpy.count(), 0);

    InputManagerTestAccess::clearPageScopeInputSuppression(&m_manager);
    InputManagerTestAccess::armHeldLeftTriggerShortPress(&m_manager);
    InputManagerTestAccess::simulateLeftTriggerReleaseEdge(&m_manager, 100);
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.at(0).at(0).toString(), QStringLiteral("previousSession"));
}

void TestInputManager::routeSwitchSuppressesSamePollButtonPress()
{
    QSignalSpy actionSpy(&m_manager, &InputManager::actionTriggered);
    QSignalSpy clickSpy(&m_manager, &InputManager::selectionClickRequested);

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    InputManagerTestAccess::clearPageScopeInputSuppression(&m_manager);
    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    QVERIFY(InputManagerTestAccess::pageScopeInputSuppressed(&m_manager));

    InputManagerTestAccess::handleButtonPress(&m_manager, SDL_CONTROLLER_BUTTON_A);
    InputManagerTestAccess::handleButtonPress(&m_manager, SDL_CONTROLLER_BUTTON_BACK);
    QCOMPARE(clickSpy.count(), 0);
    QCOMPARE(actionSpy.count(), 0);

    InputManagerTestAccess::clearPageScopeInputSuppression(&m_manager);
    InputManagerTestAccess::handleButtonPress(&m_manager, SDL_CONTROLLER_BUTTON_BACK);
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.at(0).at(0).toString(), QStringLiteral("toggleHud"));
}

void TestInputManager::dispatchNavigationKeyRespectsBrowserNavigationRoute()
{
    QSignalSpy navigationSpy(&m_manager, &InputManager::navigationKeyRequested);
    QSignalSpy browserNavigationSpy(&m_manager, &InputManager::browserNavigationKeyRequested);

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    InputManagerTestAccess::dispatchNavigationKey(&m_manager, Qt::Key_Up);
    QCOMPARE(navigationSpy.count(), 1);
    QCOMPARE(browserNavigationSpy.count(), 0);

    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::dispatchNavigationKey(&m_manager, Qt::Key_Down);
    QCOMPARE(navigationSpy.count(), 2);
    QCOMPARE(browserNavigationSpy.count(), 1);
}

void TestInputManager::adapterForwardsBrowserNavigationOnlyFromManager()
{
    InputManagerAdapter adapter(&m_manager);
    QSignalSpy navigationSpy(&adapter, &InputManagerAdapter::navigationKeyRequested);
    QSignalSpy browserNavigationSpy(&adapter, &InputManagerAdapter::browserNavigationKeyRequested);

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    InputManagerTestAccess::dispatchNavigationKey(&m_manager, Qt::Key_Left);
    QCOMPARE(navigationSpy.count(), 1);
    QCOMPARE(browserNavigationSpy.count(), 0);

    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::dispatchNavigationKey(&m_manager, Qt::Key_Right);
    QCOMPARE(navigationSpy.count(), 2);
    QCOMPARE(browserNavigationSpy.count(), 1);
}

void TestInputManager::dispatchDpadButtonLbChordDispatchesPageActions()
{
    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::clearPageScopeInputSuppression(&m_manager);
    QSignalSpy actionSpy(&m_manager, &InputManager::actionTriggered);

    InputManagerTestAccess::setLeftShoulderPressed(&m_manager, true);
    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_UP);
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.at(0).at(0).toString(), QStringLiteral("zoomIn"));

    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_DOWN);
    QCOMPARE(actionSpy.count(), 2);
    QCOMPARE(actionSpy.at(1).at(0).toString(), QStringLiteral("zoomOut"));

    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    QCOMPARE(actionSpy.count(), 3);
    QCOMPARE(actionSpy.at(2).at(0).toString(), QStringLiteral("minFontIn"));

    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    QCOMPARE(actionSpy.count(), 4);
    QCOMPARE(actionSpy.at(3).at(0).toString(), QStringLiteral("minFontOut"));

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    QCOMPARE(actionSpy.count(), 4);

    InputManagerTestAccess::setLeftShoulderPressed(&m_manager, false);
}

void TestInputManager::dispatchDpadButtonLtChordDispatchesPageActions()
{
    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::clearPageScopeInputSuppression(&m_manager);
    QSignalSpy actionSpy(&m_manager, &InputManager::actionTriggered);

    InputManagerTestAccess::setLeftTriggerPressed(&m_manager, true);
    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_LEFT);
    QCOMPARE(actionSpy.count(), 1);
    QCOMPARE(actionSpy.at(0).at(0).toString(), QStringLiteral("browserBack"));

    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    QCOMPARE(actionSpy.count(), 2);
    QCOMPARE(actionSpy.at(1).at(0).toString(), QStringLiteral("browserForward"));

    m_manager.setInputRoute(InputManager::InputRoute::UiFocus);
    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_RIGHT);
    QCOMPARE(actionSpy.count(), 2);

    InputManagerTestAccess::setLeftTriggerPressed(&m_manager, false);
}

void TestInputManager::dispatchDpadButtonRbChordFallsBackToNavigation()
{
    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::clearPageScopeInputSuppression(&m_manager);
    InputManagerTestAccess::setLeftShoulderPressed(&m_manager, false);
    QSignalSpy actionSpy(&m_manager, &InputManager::actionTriggered);
    QSignalSpy navigationSpy(&m_manager, &InputManager::navigationKeyRequested);

    InputManagerTestAccess::setRightShoulderPressed(&m_manager, true);
    InputManagerTestAccess::dispatchDpadButton(&m_manager, SDL_CONTROLLER_BUTTON_DPAD_UP);
    QCOMPARE(actionSpy.count(), 0);
    QCOMPARE(navigationSpy.count(), 1);

    InputManagerTestAccess::setRightShoulderPressed(&m_manager, false);
}

void TestInputManager::browserKeyMappingCoversEditingAndFunctionKeys()
{
    QCOMPARE(BrowserInputUtils::cefKeyForQtKey(Qt::Key_Home), 0x24);
    QCOMPARE(BrowserInputUtils::cefKeyForQtKey(Qt::Key_End), 0x23);
    QCOMPARE(BrowserInputUtils::cefKeyForQtKey(Qt::Key_Insert), 0x2D);
    QCOMPARE(BrowserInputUtils::cefKeyForQtKey(Qt::Key_Delete), 0x2E);
    QCOMPARE(BrowserInputUtils::cefKeyForQtKey(Qt::Key_F1), 0x70);
    QCOMPARE(BrowserInputUtils::cefKeyForQtKey(Qt::Key_F24), 0x87);
    QCOMPARE(BrowserInputUtils::cefKeyForQtKey(Qt::Key_Plus), 0xBB);
    QCOMPARE(BrowserInputUtils::cefKeyForQtKey(Qt::Key_Question), 0xBF);
}

void TestInputManager::browserKeyModifiersPreserveShortcuts()
{
    const int modifiers = BrowserInputUtils::cefModifiersForQtModifiers(
        Qt::ShiftModifier | Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier);
    QVERIFY(modifiers & EVENTFLAG_SHIFT_DOWN);
    QVERIFY(modifiers & EVENTFLAG_CONTROL_DOWN);
    QVERIFY(modifiers & EVENTFLAG_ALT_DOWN);
    QVERIFY(modifiers & EVENTFLAG_COMMAND_DOWN);
}

void TestInputManager::dialogOverlayDoesNotClickThroughToBrowser()
{
    BrowserView browserView;
    QWidget overlay;
    overlay.setProperty("dialogLikeOverlay", true);
    overlay.setFocusPolicy(Qt::StrongFocus);
    overlay.show();
    overlay.activateWindow();
    overlay.setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), &overlay);

    m_manager.setActiveBrowserView(&browserView);
    m_manager.setInputRoute(InputManager::InputRoute::BrowserPointer);
    InputManagerTestAccess::sendMouseButtonEvent(&m_manager, Qt::LeftButton, true);
    InputManagerTestAccess::sendMouseButtonEvent(&m_manager, Qt::LeftButton, false);

    QCOMPARE(BrowserViewTestAccess::mouseButtonEventCount(&browserView), 0);
    m_manager.setActiveBrowserView(nullptr);
}

void TestInputManager::gracePeriodDoesNotSwallowNavigationKeys()
{
    InputManagerTestAccess::armTextInputGrace(
        &m_manager, QDateTime::currentMSecsSinceEpoch() + 60'000);

    QKeyEvent downPress(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QKeyEvent rightPress(QEvent::KeyPress, Qt::Key_Right, Qt::NoModifier);
    QKeyEvent upRelease(QEvent::KeyRelease, Qt::Key_Up, Qt::NoModifier);
    QKeyEvent leftRelease(QEvent::KeyRelease, Qt::Key_Left, Qt::NoModifier);
    QVERIFY(!InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &downPress));
    QVERIFY(!InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &rightPress));
    QVERIFY(!InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &upRelease));
    QVERIFY(!InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &leftRelease));

    QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QVERIFY(InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &enterPress));

    InputManagerTestAccess::armTextInputGrace(&m_manager, 0);
}

void TestInputManager::textInputSessionSwallowsNavigationKeys()
{
    InputManagerTestAccess::setTextInputSession(&m_manager, true);

    QKeyEvent downPress(QEvent::KeyPress, Qt::Key_Down, Qt::NoModifier);
    QVERIFY(InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &downPress));

    InputManagerTestAccess::setTextInputSession(&m_manager, false);
}

void TestInputManager::enterDuringTextSessionAcknowledgesKeyboardClosed()
{
    InputManagerTestAccess::setTextInputSession(&m_manager, true);
    QVERIFY(m_manager.textInputActive());

    QKeyEvent enterPress(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
    QVERIFY(InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &enterPress));
    QVERIFY(!m_manager.textInputActive());
}

void TestInputManager::duplicateCjkKeyPressIsDeduped()
{
    QObject field;
    field.setProperty("text", QString());
    InputManagerTestAccess::setTextInputSession(&m_manager, true);
    InputManagerTestAccess::setTextInputTarget(&m_manager, &field);

    const QString glyph = QStringLiteral("得");
    QKeyEvent first(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, glyph);
    QKeyEvent second(QEvent::KeyPress, Qt::Key_unknown, Qt::NoModifier, glyph);

    QVERIFY(InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &first));
    QVERIFY(InputManagerTestAccess::redirectTextInputEvent(&m_manager, nullptr, &second));
    QCOMPARE(field.property("text").toString(), glyph);

    InputManagerTestAccess::setTextInputSession(&m_manager, false);
    InputManagerTestAccess::setTextInputTarget(&m_manager, nullptr);
}

void TestInputManager::steamAndDeckNamesMapToSteamKind()
{
    QCOMPARE(InputManager::gamepadKindFromDeviceName(QStringLiteral("Steam Controller")),
             InputManager::InputDeviceKind::GamepadSteam);
    QCOMPARE(InputManager::gamepadKindFromDeviceName(QStringLiteral("Steam Deck")),
             InputManager::InputDeviceKind::GamepadSteam);
    QCOMPARE(InputManager::gamepadKindFromDeviceName(QStringLiteral("Valve SteamDeck")),
             InputManager::InputDeviceKind::GamepadSteam);
    QCOMPARE(InputManager::inputDeviceKindIdFor(InputManager::InputDeviceKind::GamepadSteam),
             QStringLiteral("steam"));
}

void TestInputManager::virtualPadAndXboxNamesDoNotMapToSteam()
{
    QCOMPARE(InputManager::gamepadKindFromDeviceName(QStringLiteral("Steam Virtual Gamepad")),
             InputManager::InputDeviceKind::GamepadGeneric);
    QCOMPARE(InputManager::gamepadKindFromDeviceName(QStringLiteral("Microsoft X-Box 360 pad")),
             InputManager::InputDeviceKind::GamepadXbox);
    QCOMPARE(InputManager::gamepadKindFromDeviceName(QStringLiteral("Xbox Wireless Controller")),
             InputManager::InputDeviceKind::GamepadXbox);
    QCOMPARE(InputManager::gamepadKindFromDeviceName(QStringLiteral("PS5 Controller")),
             InputManager::InputDeviceKind::GamepadPlayStation);
}

QTEST_MAIN(TestInputManager)
#include "tst_inputmanager.moc"
