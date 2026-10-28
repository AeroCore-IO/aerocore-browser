#include "buttoniconlayout.h"

#include <QDir>
#include <QFileInfo>

namespace ButtonIconLayout {
namespace {

QString normalizedLayout(const QString &layout)
{
    if (layout == kPlayStation || layout == kNintendo || layout == kSteamController) {
        return layout;
    }
    return kXbox;
}

bool existsUnder(const QDir &root, const QString &relativePath)
{
    return QFileInfo::exists(root.filePath(relativePath));
}

} // namespace

QString layoutIdFor(InputManager::InputDeviceKind kind)
{
    switch (kind) {
    case InputManager::InputDeviceKind::GamepadPlayStation:
        return kPlayStation;
    case InputManager::InputDeviceKind::GamepadNintendo:
        return kNintendo;
    case InputManager::InputDeviceKind::GamepadSteam:
        return kSteamController;
    case InputManager::InputDeviceKind::GamepadXbox:
    case InputManager::InputDeviceKind::GamepadGeneric:
    case InputManager::InputDeviceKind::Unknown:
    default:
        return kXbox;
    }
}

QString resolveIconRelativePath(const QString &layout,
                                const QString &imageRoot,
                                const QString &fileName)
{
    const QDir root(imageRoot);
    const QString layoutId = normalizedLayout(layout);
    const QString candidate = layoutId + QLatin1Char('/') + fileName;
    if (existsUnder(root, candidate)) {
        return candidate;
    }
    return kXbox + QLatin1Char('/') + fileName;
}

QString adaptHelpText(const QString &layout, const QString &text)
{
    const QString layoutId = normalizedLayout(layout);
    if (layoutId == kXbox) {
        return text;
    }

    QString adapted = text;
    if (layoutId == kNintendo) {
        adapted.replace(QStringLiteral("LB"), QStringLiteral("L"));
        adapted.replace(QStringLiteral("RB"), QStringLiteral("R"));
        adapted.replace(QStringLiteral("LT"), QStringLiteral("ZL"));
        adapted.replace(QStringLiteral("RT"), QStringLiteral("ZR"));
        return adapted;
    }

    adapted.replace(QStringLiteral("LB"), QStringLiteral("L1"));
    adapted.replace(QStringLiteral("RB"), QStringLiteral("R1"));
    adapted.replace(QStringLiteral("LT"), QStringLiteral("L2"));
    adapted.replace(QStringLiteral("RT"), QStringLiteral("R2"));
    return adapted;
}

HelpJoystickPair resolveHelpJoystickPair(const QString &layout, const QString &imageRoot)
{
    const QDir root(imageRoot);
    QString layoutId = normalizedLayout(layout);
    QString overlay = layoutId + QLatin1Char('/') + kJoystickOverlay;
    if (!existsUnder(root, overlay)) {
        layoutId = kXbox;
        overlay = layoutId + QLatin1Char('/') + kJoystickOverlay;
    }

    HelpJoystickPair pair;
    pair.overlayRelativePath = overlay;
    const QString photo = layoutId + QLatin1Char('/') + kJoystickPhoto;
    if (existsUnder(root, photo)) {
        pair.photoRelativePath = photo;
    }
    return pair;
}

} // namespace ButtonIconLayout
