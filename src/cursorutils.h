#pragma once

#include <QCursor>
#include <QPixmap>
#include <QPoint>

#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
typedef struct _XDisplay Display;
typedef unsigned long KeySym;
#endif

class QScreen;

enum class CursorRenderMode {
	CustomOverlay,
	System
};

CursorRenderMode cursorRenderMode();
bool useCustomCursorRendering();
bool useSystemCursorRendering();
#if defined(AEROCORE_HAVE_X11_SYSTEM_MOUSE)
Display *sharedX11Display();
// Snapshot the server keymap on NewKeyboardNotify for KeyPress lookup, including Steam OSK CJK remaps.
void refreshX11KeymapSnapshot();
bool x11KeysymFromSnapshot(unsigned keycode, unsigned state, KeySym *keysymOut);
void clearX11UnicodeKeymapSnapshot();
#endif
int adaptiveArrowCursorSizeForScreen(const QScreen *screen);
QPoint adaptiveArrowCursorHotspotForSize(int logicalSize);
QPixmap adaptiveArrowCursorPixmapForScreen(const QScreen *screen);
QCursor adaptiveArrowCursorForScreen(const QScreen *screen);
