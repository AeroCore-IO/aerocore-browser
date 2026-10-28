#include "gamepadcatalog.h"

#include "test_util.h"

#include <QTest>

class TestGamepadCatalog : public QObject
{
    Q_OBJECT

private slots:
    void resolveAutoUsesDetectedKind();
    void resolveForcedOverridesDetected();
    void genericAndUnknownFallBackToXbox();
    void takeoverButtonOrHighStick();
    void chooseRestoresLastExclusive();
    void chooseReconnectsByGuid();
    void choosePrefersRecentlyUsedThenExternal();
    void evictOldestDisconnectedKeepsConnected();
    void detectSonyVidAsPlayStation();
    void detectSteamVirtualAsGeneric();
    void detectDeckAsSteamBuiltin();
    void detect2026SteamControllerAsSteam();
    void skipTakeoverWhenExclusiveAlsoHasButton();
    void allowTakeoverWhenExclusiveIdle();
    void chooseStartupPrefersExternalOverBuiltin();
    void chooseStartupStillRestoresLastExclusive();
    void overrideMenuIndexRoundtrip();
    void sameInstanceMatchesGuid();
    void dualSenseNameWithoutSonyWord();
};

void TestGamepadCatalog::resolveAutoUsesDetectedKind()
{
    QCOMPARE(GamepadCatalog::resolveLayoutId(QStringLiteral("playstation"), QStringLiteral("auto")),
             QStringLiteral("playstation"));
    QCOMPARE(GamepadCatalog::resolveLayoutId(QStringLiteral("steam"), QString()),
             QStringLiteral("steamcontroller"));
}

void TestGamepadCatalog::resolveForcedOverridesDetected()
{
    QCOMPARE(GamepadCatalog::resolveLayoutId(QStringLiteral("xbox"), QStringLiteral("playstation")),
             QStringLiteral("playstation"));
}

void TestGamepadCatalog::genericAndUnknownFallBackToXbox()
{
    QCOMPARE(GamepadCatalog::layoutIdForKindId(QStringLiteral("gamepad")),
             QStringLiteral("xbox"));
    QCOMPARE(GamepadCatalog::layoutIdForKindId(QStringLiteral("none")),
             QStringLiteral("xbox"));
}

void TestGamepadCatalog::takeoverButtonOrHighStick()
{
    QVERIFY(GamepadCatalog::qualifiesTakeover(true, 0.0));
    QVERIFY(!GamepadCatalog::qualifiesTakeover(false, 0.30));
    QVERIFY(!GamepadCatalog::qualifiesTakeover(false, 0.54));
    QVERIFY(GamepadCatalog::qualifiesTakeover(false, 0.55));
}

void TestGamepadCatalog::chooseRestoresLastExclusive()
{
    QVector<GamepadCatalog::DeviceRecord> connected;
    GamepadCatalog::DeviceRecord deck;
    deck.instanceId = QStringLiteral("deck");
    deck.builtin = true;
    connected.push_back(deck);
    GamepadCatalog::DeviceRecord dual;
    dual.instanceId = QStringLiteral("dual");
    connected.push_back(dual);

    QCOMPARE(GamepadCatalog::chooseExclusiveInstanceId(connected, QStringLiteral("dual")),
             QStringLiteral("dual"));
}

void TestGamepadCatalog::chooseReconnectsByGuid()
{
    QVector<GamepadCatalog::DeviceRecord> connected;
    GamepadCatalog::DeviceRecord dual;
    dual.instanceId = QStringLiteral("aabb#9");
    connected.push_back(dual);

    QCOMPARE(GamepadCatalog::chooseExclusiveInstanceId(connected, QStringLiteral("aabb#3")),
             QStringLiteral("aabb#9"));
}

void TestGamepadCatalog::choosePrefersRecentlyUsedThenExternal()
{
    QVector<GamepadCatalog::DeviceRecord> connected;
    GamepadCatalog::DeviceRecord deck;
    deck.instanceId = QStringLiteral("deck");
    deck.builtin = true;
    deck.lastUsedMs = 50;
    connected.push_back(deck);
    GamepadCatalog::DeviceRecord dual;
    dual.instanceId = QStringLiteral("dual");
    dual.lastUsedMs = 10;
    connected.push_back(dual);

    QCOMPARE(GamepadCatalog::chooseExclusiveInstanceId(connected, QStringLiteral("gone")),
             QStringLiteral("deck"));

    deck.lastUsedMs = 0;
    dual.lastUsedMs = 0;
    connected = {deck, dual};
    QCOMPARE(GamepadCatalog::chooseExclusiveInstanceId(connected, QString()),
             QStringLiteral("dual"));
}

void TestGamepadCatalog::evictOldestDisconnectedKeepsConnected()
{
    QVector<GamepadCatalog::DeviceRecord> known;
    for (int i = 0; i < 8; ++i) {
        GamepadCatalog::DeviceRecord record;
        record.instanceId = QStringLiteral("old%1").arg(i);
        record.lastUsedMs = i;
        known.push_back(record);
    }
    GamepadCatalog::DeviceRecord live;
    live.instanceId = QStringLiteral("live");
    live.lastUsedMs = 0;
    known.push_back(live);

    GamepadCatalog::DeviceRecord incoming;
    incoming.instanceId = QStringLiteral("new");
    incoming.lastUsedMs = 100;
    GamepadCatalog::upsertDevice(known, incoming, {QStringLiteral("live")});

    QVERIFY(GamepadCatalog::findRecordIndex(known, QStringLiteral("live")) >= 0);
    QVERIFY(GamepadCatalog::findRecordIndex(known, QStringLiteral("new")) >= 0);
    QVERIFY(GamepadCatalog::findRecordIndex(known, QStringLiteral("old0")) < 0);
    QCOMPARE(known.size(), GamepadCatalog::kMaxRememberedDevices);
}

void TestGamepadCatalog::detectSonyVidAsPlayStation()
{
    QCOMPARE(GamepadCatalog::detectKindId(0x054C, 0x0CE6, 1, QStringLiteral("Xbox 360 Controller")),
             QStringLiteral("playstation"));
}

void TestGamepadCatalog::detectSteamVirtualAsGeneric()
{
    QCOMPARE(GamepadCatalog::detectKindId(0x28DE, 0x11FF, 1, QStringLiteral("Steam Virtual Gamepad")),
             QStringLiteral("gamepad"));
    QVERIFY(GamepadCatalog::isSteamVirtualGamepad(0x28DE, 0x11FF, QStringLiteral("Steam Virtual Gamepad")));
}

void TestGamepadCatalog::detectDeckAsSteamBuiltin()
{
    QCOMPARE(GamepadCatalog::detectKindId(0x28DE, 0x1205, 0, QStringLiteral("Steam Deck")),
             QStringLiteral("steam"));
    QVERIFY(GamepadCatalog::isBuiltinSteamDevice(0x28DE, 0x1205, QStringLiteral("Steam Deck")));
}

void TestGamepadCatalog::detect2026SteamControllerAsSteam()
{
    QCOMPARE(GamepadCatalog::detectKindId(0x28DE, 0x1302, 0, QStringLiteral("Steam Controller")),
             QStringLiteral("steam"));
    QVERIFY(!GamepadCatalog::isBuiltinSteamDevice(0x28DE, 0x1302, QStringLiteral("Steam Controller")));
}

void TestGamepadCatalog::skipTakeoverWhenExclusiveAlsoHasButton()
{
    QVERIFY(!GamepadCatalog::shouldTakeOverFromCandidate(true, 0.0, true));
    QVERIFY(!GamepadCatalog::shouldTakeOverFromCandidate(false, 0.80, true));
}

void TestGamepadCatalog::allowTakeoverWhenExclusiveIdle()
{
    QVERIFY(GamepadCatalog::shouldTakeOverFromCandidate(true, 0.0, false));
    QVERIFY(GamepadCatalog::shouldTakeOverFromCandidate(false, 0.80, false));
}

void TestGamepadCatalog::chooseStartupPrefersExternalOverBuiltin()
{
    QVector<GamepadCatalog::DeviceRecord> connected;
    GamepadCatalog::DeviceRecord deck;
    deck.instanceId = QStringLiteral("deck");
    deck.builtin = true;
    deck.lastUsedMs = 50;
    connected.push_back(deck);
    GamepadCatalog::DeviceRecord dual;
    dual.instanceId = QStringLiteral("dual");
    dual.lastUsedMs = 0;
    connected.push_back(dual);

    QCOMPARE(GamepadCatalog::chooseExclusiveInstanceId(
                 connected,
                 QStringLiteral("gone"),
                 GamepadCatalog::ExclusiveFallbackPolicy::ExternalThenBuiltin),
             QStringLiteral("dual"));
    QCOMPARE(GamepadCatalog::chooseExclusiveInstanceId(
                 connected,
                 QStringLiteral("gone"),
                 GamepadCatalog::ExclusiveFallbackPolicy::RecentlyUsedThenExternal),
             QStringLiteral("deck"));
}

void TestGamepadCatalog::chooseStartupStillRestoresLastExclusive()
{
    QVector<GamepadCatalog::DeviceRecord> connected;
    GamepadCatalog::DeviceRecord deck;
    deck.instanceId = QStringLiteral("deck");
    deck.builtin = true;
    deck.lastUsedMs = 50;
    connected.push_back(deck);
    GamepadCatalog::DeviceRecord dual;
    dual.instanceId = QStringLiteral("dual");
    dual.lastUsedMs = 0;
    connected.push_back(dual);

    QCOMPARE(GamepadCatalog::chooseExclusiveInstanceId(
                 connected,
                 QStringLiteral("deck"),
                 GamepadCatalog::ExclusiveFallbackPolicy::ExternalThenBuiltin),
             QStringLiteral("deck"));
}

void TestGamepadCatalog::overrideMenuIndexRoundtrip()
{
    QCOMPARE(GamepadCatalog::overrideMenuIndex(QStringLiteral("auto")), 0);
    QCOMPARE(GamepadCatalog::overrideFromMenuIndex(1), QStringLiteral("xbox"));
    QCOMPARE(GamepadCatalog::overrideMenuIndex(QStringLiteral("playstation")), 2);
    QCOMPARE(GamepadCatalog::overrideFromMenuIndex(0), QStringLiteral("auto"));
    QCOMPARE(GamepadCatalog::normalizeLayoutId(QStringLiteral("auto")), QStringLiteral("xbox"));
}

void TestGamepadCatalog::sameInstanceMatchesGuid()
{
    QVERIFY(GamepadCatalog::sameInstance(QStringLiteral("aabb#1"), QStringLiteral("aabb#9")));
    QVERIFY(!GamepadCatalog::sameInstance(QStringLiteral("aabb#1"), QStringLiteral("ccdd#1")));
    QVector<QString> ids{QStringLiteral("aabb#9")};
    QVERIFY(GamepadCatalog::containsInstance(ids, QStringLiteral("aabb#1")));
    QVERIFY(!GamepadCatalog::containsInstance(ids, QStringLiteral("ccdd")));
}

void TestGamepadCatalog::dualSenseNameWithoutSonyWord()
{
    QCOMPARE(GamepadCatalog::kindIdFromName(QStringLiteral("DualSense Wireless Controller")),
             QStringLiteral("playstation"));
}

QTEST_CORE_MAIN(TestGamepadCatalog)
#include "tst_gamepadcatalog.moc"
