#include "sources/dashboard/rendering/lol_key_labels.hpp"

#include <uiohook.h>

namespace sources {

QString lol_dashboard_key_label(uint16_t code)
{
	if (code >= VC_A && code <= VC_Z)
		return QString(QChar('A' + code - VC_A));
	if (code >= VC_0 && code <= VC_9)
		return QString(QChar('0' + code - VC_0));
	if (code >= VC_F1 && code <= VC_F12)
		return QString("F%1").arg(code - VC_F1 + 1);
	switch (code) {
	case VC_SPACE:
		return "␣";
	case VC_SHIFT_L:
	case VC_SHIFT_R:
		return "⇧";
	case VC_CONTROL_L:
	case VC_CONTROL_R:
		return "⌃";
	case VC_ALT_L:
	case VC_ALT_R:
		return "⌥";
	case VC_META_L:
	case VC_META_R:
		return "⌘";
	case VC_FUNCTION:
		return "fn";
	case VC_TAB:
		return "⇥";
	case VC_CAPS_LOCK:
		return "⇪";
	case VC_BACKSPACE:
		return "⌫";
	case VC_ENTER:
		return "↵";
	case VC_ESCAPE:
		return "⎋";
	case VC_MINUS:
		return "-";
	case VC_EQUALS:
		return "=";
	case VC_OPEN_BRACKET:
		return "[";
	case VC_CLOSE_BRACKET:
		return "]";
	case VC_BACK_SLASH:
	case VC_102:
		return "\\";
	case VC_SEMICOLON:
		return ";";
	case VC_QUOTE:
		return "'";
	case VC_COMMA:
		return ",";
	case VC_PERIOD:
		return ".";
	case VC_SLASH:
		return "/";
	case VC_BACK_QUOTE:
		return "`";
	case VC_UP:
		return "↑";
	case VC_DOWN:
		return "↓";
	case VC_LEFT:
		return "←";
	case VC_RIGHT:
		return "→";
	case VC_HOME:
		return "Home";
	case VC_END:
		return "End";
	case VC_PAGE_UP:
		return "Pg↑";
	case VC_PAGE_DOWN:
		return "Pg↓";
	case VC_INSERT:
		return "Ins";
	case VC_DELETE:
		return "⌦";
	case VC_NUM_LOCK:
		return "Num";
	case VC_KP_DIVIDE:
		return "KP /";
	case VC_KP_MULTIPLY:
		return "KP *";
	case VC_KP_SUBTRACT:
		return "KP -";
	case VC_KP_ADD:
		return "KP +";
	case VC_KP_ENTER:
		return "KP ↵";
	case VC_KP_DECIMAL:
		return "KP .";
	case VC_KP_0:
	case VC_KP_1:
	case VC_KP_2:
	case VC_KP_3:
	case VC_KP_4:
	case VC_KP_5:
	case VC_KP_6:
	case VC_KP_7:
	case VC_KP_8:
	case VC_KP_9:
		return QString("KP %1").arg(code - VC_KP_0);
	default:
		return "?";
	}
}

} // namespace sources
