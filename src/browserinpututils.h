#pragma once

#include <Qt>

#include <include/internal/cef_types.h>

namespace BrowserInputUtils {

inline int cefKeyForQtKey(int key)
{
    if (key >= Qt::Key_F1 && key <= Qt::Key_F24) {
        return 0x70 + (key - Qt::Key_F1);
    }

    switch (key) {
    case Qt::Key_Shift:
        return 0x10;
    case Qt::Key_Control:
        return 0x11;
    case Qt::Key_Alt:
        return 0x12;
    case Qt::Key_Pause:
        return 0x13;
    case Qt::Key_CapsLock:
        return 0x14;
    case Qt::Key_Meta:
        return 0x5B;
    case Qt::Key_Left:
        return 0x25;
    case Qt::Key_Up:
        return 0x26;
    case Qt::Key_Right:
        return 0x27;
    case Qt::Key_Down:
        return 0x28;
    case Qt::Key_PageUp:
        return 0x21;
    case Qt::Key_PageDown:
        return 0x22;
    case Qt::Key_Home:
        return 0x24;
    case Qt::Key_End:
        return 0x23;
    case Qt::Key_Insert:
        return 0x2D;
    case Qt::Key_Delete:
        return 0x2E;
    case Qt::Key_Print:
        return 0x2C;
    case Qt::Key_Backspace:
        return 0x08;
    case Qt::Key_Tab:
        return 0x09;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return 0x0D;
    case Qt::Key_Escape:
        return 0x1B;
    case Qt::Key_Space:
        return 0x20;
    case Qt::Key_Semicolon:
    case Qt::Key_Colon:
        return 0xBA;
    case Qt::Key_Equal:
    case Qt::Key_Plus:
        return 0xBB;
    case Qt::Key_Comma:
    case Qt::Key_Less:
        return 0xBC;
    case Qt::Key_Minus:
    case Qt::Key_Underscore:
        return 0xBD;
    case Qt::Key_Period:
    case Qt::Key_Greater:
        return 0xBE;
    case Qt::Key_Slash:
    case Qt::Key_Question:
        return 0xBF;
    case Qt::Key_QuoteLeft:
    case Qt::Key_AsciiTilde:
        return 0xC0;
    case Qt::Key_BracketLeft:
    case Qt::Key_BraceLeft:
        return 0xDB;
    case Qt::Key_Backslash:
    case Qt::Key_Bar:
        return 0xDC;
    case Qt::Key_BracketRight:
    case Qt::Key_BraceRight:
        return 0xDD;
    case Qt::Key_Apostrophe:
    case Qt::Key_QuoteDbl:
        return 0xDE;
    case Qt::Key_NumLock:
        return 0x90;
    case Qt::Key_ScrollLock:
        return 0x91;
    default:
        return key >= 0 && key <= 0xFF ? key : 0;
    }
}

inline int cefModifiersForQtModifiers(Qt::KeyboardModifiers modifiers)
{
    int result = EVENTFLAG_NONE;
    if (modifiers.testFlag(Qt::ShiftModifier)) {
        result |= EVENTFLAG_SHIFT_DOWN;
    }
    if (modifiers.testFlag(Qt::ControlModifier)) {
        result |= EVENTFLAG_CONTROL_DOWN;
    }
    if (modifiers.testFlag(Qt::AltModifier)) {
        result |= EVENTFLAG_ALT_DOWN;
    }
    if (modifiers.testFlag(Qt::MetaModifier)) {
        result |= EVENTFLAG_COMMAND_DOWN;
    }
    return result;
}

} // namespace BrowserInputUtils
