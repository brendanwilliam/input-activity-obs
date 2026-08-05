#include "sources/dashboard/rendering/lol_key_labels.hpp"

#include "input/keycodes.h"

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
	case VC_UP:
		return "↑";
	case VC_DOWN:
		return "↓";
	case VC_LEFT:
		return "←";
	case VC_RIGHT:
		return "→";
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
	case VC_TAB:
		return "⇥";
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
	default:
		return "?";
	}
}
} // namespace sources
