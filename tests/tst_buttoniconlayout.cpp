#include "buttoniconlayout.h"

#include "test_util.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

class TestButtonIconLayout : public QObject
{
    Q_OBJECT

private:
    static bool writeEmptyFile(const QString &path)
    {
        QDir().mkpath(QFileInfo(path).absolutePath());
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly)) {
            return false;
        }
        file.write("x");
        return true;
    }

private slots:
    void layoutIdForKinds();
    void resolveFallsBackToXboxWhenMissing();
    void resolveUsesLayoutWhenPresent();
    void helpJoystickFallsBackTogetherWhenOverlayMissing();
    void helpJoystickKeepsLayoutWhenOverlayPresent();
    void adaptPlayStationTokens();
    void adaptNintendoTokensLongFirst();
    void adaptSteamControllerTokens();
    void adaptXboxUnchanged();
};

void TestButtonIconLayout::layoutIdForKinds()
{
    QCOMPARE(ButtonIconLayout::layoutIdFor(InputManager::InputDeviceKind::GamepadXbox),
             QStringLiteral("xbox"));
    QCOMPARE(ButtonIconLayout::layoutIdFor(InputManager::InputDeviceKind::GamepadPlayStation),
             QStringLiteral("playstation"));
    QCOMPARE(ButtonIconLayout::layoutIdFor(InputManager::InputDeviceKind::GamepadNintendo),
             QStringLiteral("nintendo"));
    QCOMPARE(ButtonIconLayout::layoutIdFor(InputManager::InputDeviceKind::GamepadSteam),
             QStringLiteral("steamcontroller"));
    QCOMPARE(ButtonIconLayout::layoutIdFor(InputManager::InputDeviceKind::GamepadGeneric),
             QStringLiteral("xbox"));
    QCOMPARE(ButtonIconLayout::layoutIdFor(InputManager::InputDeviceKind::Unknown),
             QStringLiteral("xbox"));
}

void TestButtonIconLayout::resolveFallsBackToXboxWhenMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("xbox/op-direction.svg"))));

    QCOMPARE(ButtonIconLayout::resolveIconRelativePath(
                 QStringLiteral("playstation"), dir.path(), QStringLiteral("op-direction.svg")),
             QStringLiteral("xbox/op-direction.svg"));
}

void TestButtonIconLayout::resolveUsesLayoutWhenPresent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("xbox/op-keya.svg"))));
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("playstation/op-keya.svg"))));

    QCOMPARE(ButtonIconLayout::resolveIconRelativePath(
                 QStringLiteral("playstation"), dir.path(), QStringLiteral("op-keya.svg")),
             QStringLiteral("playstation/op-keya.svg"));
}

void TestButtonIconLayout::helpJoystickFallsBackTogetherWhenOverlayMissing()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("xbox/joystick.svg"))));
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("xbox/joystick.png"))));
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("playstation/joystick.png"))));

    const auto pair = ButtonIconLayout::resolveHelpJoystickPair(
        QStringLiteral("playstation"), dir.path());
    QCOMPARE(pair.overlayRelativePath, QStringLiteral("xbox/joystick.svg"));
    QCOMPARE(pair.photoRelativePath, QStringLiteral("xbox/joystick.png"));
}

void TestButtonIconLayout::helpJoystickKeepsLayoutWhenOverlayPresent()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("xbox/joystick.svg"))));
    QVERIFY(writeEmptyFile(dir.filePath(QStringLiteral("playstation/joystick.svg"))));

    const auto pair = ButtonIconLayout::resolveHelpJoystickPair(
        QStringLiteral("playstation"), dir.path());
    QCOMPARE(pair.overlayRelativePath, QStringLiteral("playstation/joystick.svg"));
    QVERIFY(pair.photoRelativePath.isEmpty());
}

void TestButtonIconLayout::adaptPlayStationTokens()
{
    QCOMPARE(ButtonIconLayout::adaptHelpText(
                 QStringLiteral("playstation"), QStringLiteral("短按 LB / RB")),
             QStringLiteral("短按 L1 / R1"));
}

void TestButtonIconLayout::adaptNintendoTokensLongFirst()
{
    QCOMPARE(ButtonIconLayout::adaptHelpText(
                 QStringLiteral("nintendo"), QStringLiteral("LB+方向键；LT 切换")),
             QStringLiteral("L+方向键；ZL 切换"));
}

void TestButtonIconLayout::adaptSteamControllerTokens()
{
    QCOMPARE(ButtonIconLayout::adaptHelpText(
                 QStringLiteral("steamcontroller"), QStringLiteral("LB / LT")),
             QStringLiteral("L1 / L2"));
}

void TestButtonIconLayout::adaptXboxUnchanged()
{
    QCOMPARE(ButtonIconLayout::adaptHelpText(
                 QStringLiteral("xbox"), QStringLiteral("LB / LT")),
             QStringLiteral("LB / LT"));
}

QTEST_CORE_MAIN(TestButtonIconLayout)
#include "tst_buttoniconlayout.moc"
