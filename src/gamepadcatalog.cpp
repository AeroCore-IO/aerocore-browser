#include "gamepadcatalog.h"

#include <algorithm>
#include <limits>

namespace GamepadCatalog {
namespace {

bool containsIgnoreCase(const QString &haystack, const QString &needle)
{
    return haystack.contains(needle, Qt::CaseInsensitive);
}

} // namespace

QString guidOf(const QString &instanceId)
{
    const int hash = instanceId.indexOf(QLatin1Char('#'));
    return hash < 0 ? instanceId : instanceId.left(hash);
}

bool sameInstance(const QString &left, const QString &right)
{
    if (left == right) {
        return true;
    }
    const QString leftGuid = guidOf(left);
    return !leftGuid.isEmpty() && leftGuid == guidOf(right);
}

bool containsInstance(const QVector<QString> &ids, const QString &instanceId)
{
    for (const QString &id : ids) {
        if (sameInstance(id, instanceId)) {
            return true;
        }
    }
    return false;
}

QString normalizeOverride(const QString &override)
{
    const QString trimmed = override.trimmed().toLower();
    if (trimmed == kLayoutXbox || trimmed == kLayoutPlayStation
        || trimmed == kLayoutNintendo || trimmed == kLayoutSteam) {
        return trimmed;
    }
    return kOverrideAuto;
}

QString normalizeLayoutId(const QString &layout)
{
    const QString trimmed = layout.trimmed().toLower();
    if (trimmed == kLayoutPlayStation || trimmed == kLayoutNintendo
        || trimmed == kLayoutSteam || trimmed == kLayoutXbox) {
        return trimmed;
    }
    return kLayoutXbox;
}

int overrideMenuIndex(const QString &override)
{
    const QString normalized = normalizeOverride(override);
    if (normalized == kLayoutXbox) {
        return 1;
    }
    if (normalized == kLayoutPlayStation) {
        return 2;
    }
    if (normalized == kLayoutNintendo) {
        return 3;
    }
    if (normalized == kLayoutSteam) {
        return 4;
    }
    return 0;
}

QString overrideFromMenuIndex(int index)
{
    switch (index) {
    case 1:
        return kLayoutXbox;
    case 2:
        return kLayoutPlayStation;
    case 3:
        return kLayoutNintendo;
    case 4:
        return kLayoutSteam;
    default:
        return kOverrideAuto;
    }
}

bool isForcedLayout(const QString &override)
{
    return normalizeOverride(override) != kOverrideAuto;
}

QString layoutIdForKindId(const QString &kindId)
{
    if (kindId == kKindPlayStation) {
        return kLayoutPlayStation;
    }
    if (kindId == kKindNintendo) {
        return kLayoutNintendo;
    }
    if (kindId == kKindSteam) {
        return kLayoutSteam;
    }
    return kLayoutXbox;
}

QString resolveLayoutId(const QString &detectedKindId, const QString &layoutOverride)
{
    const QString normalized = normalizeOverride(layoutOverride);
    if (normalized != kOverrideAuto) {
        return normalized;
    }
    return layoutIdForKindId(detectedKindId);
}

QString layoutDisplayName(const QString &layoutId)
{
    if (layoutId == kLayoutPlayStation) {
        return QStringLiteral("PlayStation");
    }
    if (layoutId == kLayoutNintendo) {
        return QStringLiteral("Nintendo");
    }
    if (layoutId == kLayoutSteam) {
        return QStringLiteral("Steam");
    }
    return QStringLiteral("Xbox");
}

bool isSteamVirtualGamepad(quint16 vendor, quint16 product, const QString &name)
{
    if (vendor == 0x28DE && product == 0x11FF) {
        return true;
    }
    return containsIgnoreCase(name, QStringLiteral("steam virtual"));
}

bool isSteamHardwareProduct(quint16 product)
{
    switch (product) {
    case 0x1102: // original Steam Controller
    case 0x1142: // original wireless dongle
    case 0x1204:
    case 0x1205: // Steam Deck
    case 0x1302: // 2026 Steam Controller (Ibex) wired
    case 0x1303: // 2026 BLE
    case 0x1304: // 2026 Proteus puck
    case 0x1305: // 2026 Nereid
        return true;
    default:
        return false;
    }
}

bool isBuiltinSteamDevice(quint16 vendor, quint16 product, const QString &name)
{
    if (isSteamVirtualGamepad(vendor, product, name)) {
        return false;
    }
    if (vendor == 0x28DE && (product == 0x1205 || product == 0x1204)) {
        return true;
    }
    return containsIgnoreCase(name, QStringLiteral("steam deck"))
        || containsIgnoreCase(name, QStringLiteral("steamdeck"));
}

bool qualifiesTakeover(bool buttonRisingEdge, qreal maxStickMagnitude)
{
    if (buttonRisingEdge) {
        return true;
    }
    return maxStickMagnitude >= kTakeoverStickThreshold;
}

bool shouldTakeOverFromCandidate(bool candidateButtonRising, qreal candidateStick,
                                 bool exclusiveButtonRising)
{
    if (!qualifiesTakeover(candidateButtonRising, candidateStick)) {
        return false;
    }
    if (exclusiveButtonRising) {
        return false;
    }
    return true;
}

std::optional<QString> kindIdFromVidPid(quint16 vendor, quint16 product)
{
    if (vendor == 0 && product == 0) {
        return std::nullopt;
    }
    if (vendor == 0x054C) {
        return kKindPlayStation;
    }
    if (vendor == 0x057E) {
        return kKindNintendo;
    }
    if (vendor == 0x045E) {
        return kKindXbox;
    }
    if (vendor == 0x28DE) {
        if (product == 0x11FF) {
            return kKindGeneric;
        }
        if (isSteamHardwareProduct(product)) {
            return kKindSteam;
        }
        return kKindGeneric;
    }
    return std::nullopt;
}

QString kindIdFromName(const QString &name)
{
    const QString lower = name.toLower();
    if (lower.contains(QStringLiteral("steam controller"))
        || lower.contains(QStringLiteral("steam deck"))
        || lower.contains(QStringLiteral("steamdeck"))) {
        return kKindSteam;
    }
    if (lower.contains(QStringLiteral("xbox")) || lower.contains(QStringLiteral("microsoft"))) {
        return kKindXbox;
    }
    if (lower.contains(QStringLiteral("sony"))
        || lower.contains(QStringLiteral("dualshock"))
        || lower.contains(QStringLiteral("dualsense"))
        || lower.contains(QStringLiteral("playstation"))
        || lower.contains(QStringLiteral("ps3"))
        || lower.contains(QStringLiteral("ps4"))
        || lower.contains(QStringLiteral("ps5"))) {
        return kKindPlayStation;
    }
    if (lower.contains(QStringLiteral("nintendo"))
        || lower.contains(QStringLiteral("switch"))
        || lower.contains(QStringLiteral("joy-con"))
        || lower.contains(QStringLiteral("joycon"))
        || lower.contains(QStringLiteral("pro controller"))) {
        return kKindNintendo;
    }
    return kKindGeneric;
}

QString kindIdFromSdlControllerType(int sdlControllerType)
{
    switch (sdlControllerType) {
    case 1: // XBOX360
    case 2: // XBOXONE
        return kKindXbox;
    case 3: // PS3
    case 4: // PS4
    case 7: // PS5
        return kKindPlayStation;
    case 5: // SWITCH_PRO
    case 11: // JOYCON_LEFT (SDL 2.0.14+)
    case 12: // JOYCON_RIGHT
    case 13: // JOYCON_PAIR
        return kKindNintendo;
    default:
        return kKindGeneric;
    }
}

QString detectKindId(quint16 vendor, quint16 product, int sdlControllerType, const QString &name)
{
    if (const auto fromVid = kindIdFromVidPid(vendor, product)) {
        if (*fromVid != kKindGeneric || isSteamVirtualGamepad(vendor, product, name)) {
            return *fromVid;
        }
    }

    const QString fromSdl = kindIdFromSdlControllerType(sdlControllerType);
    if (fromSdl != kKindGeneric) {
        return fromSdl;
    }

    return kindIdFromName(name);
}

QString chooseExclusiveInstanceId(const QVector<DeviceRecord> &connected,
                                  const QString &lastExclusiveId,
                                  ExclusiveFallbackPolicy fallback)
{
    if (connected.isEmpty()) {
        return {};
    }

    const auto findExact = [&](const QString &id) -> QString {
        for (const DeviceRecord &record : connected) {
            if (record.instanceId == id) {
                return record.instanceId;
            }
        }
        return {};
    };

    if (const QString exact = findExact(lastExclusiveId); !exact.isEmpty()) {
        return exact;
    }

    const QString lastGuid = guidOf(lastExclusiveId);
    if (!lastGuid.isEmpty()) {
        for (const DeviceRecord &record : connected) {
            if (guidOf(record.instanceId) == lastGuid) {
                return record.instanceId;
            }
        }
    }

    if (fallback == ExclusiveFallbackPolicy::RecentlyUsedThenExternal) {
        DeviceRecord bestUsed;
        bool haveUsed = false;
        for (const DeviceRecord &record : connected) {
            if (record.lastUsedMs <= 0) {
                continue;
            }
            if (!haveUsed || record.lastUsedMs > bestUsed.lastUsedMs) {
                bestUsed = record;
                haveUsed = true;
            }
        }
        if (haveUsed) {
            return bestUsed.instanceId;
        }
    }

    QVector<DeviceRecord> sorted = connected;
    std::sort(sorted.begin(), sorted.end(), [](const DeviceRecord &left, const DeviceRecord &right) {
        if (left.builtin != right.builtin) {
            return !left.builtin && right.builtin;
        }
        return left.instanceId < right.instanceId;
    });
    return sorted.first().instanceId;
}

int findRecordIndex(const QVector<DeviceRecord> &records, const QString &instanceId)
{
    for (int index = 0; index < records.size(); ++index) {
        if (records[index].instanceId == instanceId) {
            return index;
        }
    }
    const QString guid = guidOf(instanceId);
    if (guid.isEmpty()) {
        return -1;
    }
    for (int index = 0; index < records.size(); ++index) {
        if (guidOf(records[index].instanceId) == guid) {
            return index;
        }
    }
    return -1;
}

void upsertDevice(QVector<DeviceRecord> &known,
                  const DeviceRecord &incoming,
                  const QVector<QString> &connectedIds)
{
    int index = -1;
    for (int i = 0; i < known.size(); ++i) {
        if (known[i].instanceId == incoming.instanceId) {
            index = i;
            break;
        }
    }
    if (index < 0) {
        for (int i = 0; i < known.size(); ++i) {
            if (guidOf(known[i].instanceId) == guidOf(incoming.instanceId)
                && guidOf(incoming.instanceId) == incoming.instanceId) {
                index = i;
                break;
            }
        }
    }

    if (index >= 0) {
        DeviceRecord &existing = known[index];
        existing.instanceId = incoming.instanceId;
        if (!incoming.name.isEmpty()) {
            existing.name = incoming.name;
        }
        if (!incoming.detectedKindId.isEmpty()) {
            existing.detectedKindId = incoming.detectedKindId;
        }
        existing.builtin = incoming.builtin;
        if (incoming.lastUsedMs > existing.lastUsedMs) {
            existing.lastUsedMs = incoming.lastUsedMs;
        }
        if (isForcedLayout(incoming.layoutOverride)) {
            existing.layoutOverride = normalizeOverride(incoming.layoutOverride);
        }
    } else {
        DeviceRecord stored = incoming;
        stored.layoutOverride = normalizeOverride(incoming.layoutOverride);
        known.push_back(stored);
    }

    evictDisconnectedOverCap(known, connectedIds);
}

bool forgetDevice(QVector<DeviceRecord> &known, const QString &instanceId)
{
    const int index = findRecordIndex(known, instanceId);
    if (index < 0) {
        return false;
    }
    known.removeAt(index);
    return true;
}

void evictDisconnectedOverCap(QVector<DeviceRecord> &known, const QVector<QString> &connectedIds)
{
    const auto isConnected = [&](const QString &id) {
        return containsInstance(connectedIds, id);
    };

    while (known.size() > kMaxRememberedDevices) {
        int victim = -1;
        qint64 oldest = (std::numeric_limits<qint64>::max)();
        for (int i = 0; i < known.size(); ++i) {
            if (isConnected(known[i].instanceId)) {
                continue;
            }
            if (known[i].lastUsedMs < oldest) {
                oldest = known[i].lastUsedMs;
                victim = i;
            }
        }
        if (victim < 0) {
            break;
        }
        known.removeAt(victim);
    }
}

} // namespace GamepadCatalog
