#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <optional>

namespace GamepadCatalog {

inline constexpr int kMaxRememberedDevices = 8;
inline constexpr qreal kTakeoverStickThreshold = 0.55;

inline const QString kOverrideAuto = QStringLiteral("auto");
inline const QString kLayoutXbox = QStringLiteral("xbox");
inline const QString kLayoutPlayStation = QStringLiteral("playstation");
inline const QString kLayoutNintendo = QStringLiteral("nintendo");
inline const QString kLayoutSteam = QStringLiteral("steamcontroller");

inline const QString kKindXbox = QStringLiteral("xbox");
inline const QString kKindPlayStation = QStringLiteral("playstation");
inline const QString kKindNintendo = QStringLiteral("nintendo");
inline const QString kKindSteam = QStringLiteral("steam");
inline const QString kKindGeneric = QStringLiteral("gamepad");

struct DeviceRecord {
    QString instanceId;
    QString name;
    QString detectedKindId = kKindGeneric;
    QString layoutOverride = kOverrideAuto;
    qint64 lastUsedMs = 0;
    bool builtin = false;
};

QString guidOf(const QString &instanceId);
bool sameInstance(const QString &left, const QString &right);
bool containsInstance(const QVector<QString> &ids, const QString &instanceId);
QString normalizeOverride(const QString &override);
QString normalizeLayoutId(const QString &layoutId);
int overrideMenuIndex(const QString &override);
QString overrideFromMenuIndex(int index);
bool isForcedLayout(const QString &override);
QString layoutIdForKindId(const QString &kindId);
QString resolveLayoutId(const QString &detectedKindId, const QString &layoutOverride);
QString layoutDisplayName(const QString &layoutId);

bool isSteamVirtualGamepad(quint16 vendor, quint16 product, const QString &name);
bool isBuiltinSteamDevice(quint16 vendor, quint16 product, const QString &name);
bool isSteamHardwareProduct(quint16 product);
bool qualifiesTakeover(bool buttonRisingEdge, qreal maxStickMagnitude);
bool shouldTakeOverFromCandidate(bool candidateButtonRising, qreal candidateStick,
                                 bool exclusiveButtonRising);

enum class ExclusiveFallbackPolicy {
    RecentlyUsedThenExternal,
    ExternalThenBuiltin
};

std::optional<QString> kindIdFromVidPid(quint16 vendor, quint16 product);
QString kindIdFromName(const QString &name);
QString kindIdFromSdlControllerType(int sdlControllerType);
QString detectKindId(quint16 vendor, quint16 product, int sdlControllerType, const QString &name);

QString chooseExclusiveInstanceId(
    const QVector<DeviceRecord> &connected,
    const QString &lastExclusiveId,
    ExclusiveFallbackPolicy fallback = ExclusiveFallbackPolicy::RecentlyUsedThenExternal);

int findRecordIndex(const QVector<DeviceRecord> &records, const QString &instanceId);
void upsertDevice(QVector<DeviceRecord> &known,
                  const DeviceRecord &incoming,
                  const QVector<QString> &connectedIds);
bool forgetDevice(QVector<DeviceRecord> &known, const QString &instanceId);
void evictDisconnectedOverCap(QVector<DeviceRecord> &known, const QVector<QString> &connectedIds);

} // namespace GamepadCatalog
