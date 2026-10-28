#pragma once

#if defined(__linux__)

#include <QAbstractNativeEventFilter>
#include <QByteArray>

// WORKAROUND for Steam OSK / gamescope on xcb:
// 1) QTBUG-114334 — after XI2 HIERARCHY changes, Qt may SIGSEGV on stale
//    touch/gesture device cache.
// 2) QXcbKeyboard::updateKeymap() — Steam OSK hotplug sends XKB MapNotify /
//    NewKeyboardNotify / MappingNotify; Qt then crashes in
//    xkb_state_unref / xkb_keymap_unref (seen on Bazzite + portable Qt).
class XcbXi2CrashGuard final : public QAbstractNativeEventFilter
{
public:
    XcbXi2CrashGuard();

    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;

    bool isReady() const;
    bool isActive() const { return m_xi2Active; }
    bool keymapFilterActive() const { return m_keymapFilterActive; }

    // Force keymap-update filtering while Steam OSK / text session is open.
    void setTextInputSessionActive(bool active);

    static bool shouldInstall();

private:
    int m_xinputOpcode = -1;
    int m_xkbFirstEvent = -1;
    bool m_xi2Active = false;
    bool m_keymapFilterActive = false;
    bool m_textInputSessionActive = false;
};

void installCrashBacktraceHandlers();

#endif // __linux__
