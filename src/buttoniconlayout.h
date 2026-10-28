#pragma once

#include "inputmanager.h"

#include <QString>

namespace ButtonIconLayout {

inline const QString kXbox = QStringLiteral("xbox");
inline const QString kPlayStation = QStringLiteral("playstation");
inline const QString kNintendo = QStringLiteral("nintendo");
inline const QString kSteamController = QStringLiteral("steamcontroller");

inline const QString kJoystickOverlay = QStringLiteral("joystick.svg");
inline const QString kJoystickPhoto = QStringLiteral("joystick.png");

QString layoutIdFor(InputManager::InputDeviceKind kind);

QString resolveIconRelativePath(const QString &layout,
                                const QString &imageRoot,
                                const QString &fileName);

QString adaptHelpText(const QString &layout, const QString &text);

struct HelpJoystickPair {
    QString overlayRelativePath;
    QString photoRelativePath;
};

HelpJoystickPair resolveHelpJoystickPair(const QString &layout, const QString &imageRoot);

} // namespace ButtonIconLayout
