
QString key_name(const input_data::trace_event &event)
{
	if (event.keychar >= 32 && event.keychar < 127)
		return QString(QChar(event.keychar)).toUpper();
	if (event.code >= VC_A && event.code <= VC_Z)
		return QString(QChar('A' + event.code - VC_A));
	if (event.code >= VC_0 && event.code <= VC_9)
		return QString(QChar('0' + event.code - VC_0));
	if (event.code >= VC_F1 && event.code <= VC_F12)
		return QString("F%1").arg(event.code - VC_F1 + 1);
	if (event.code >= VC_F13 && event.code <= VC_F24)
		return QString("F%1").arg(event.code - VC_F13 + 13);
	if (event.code >= VC_KP_0 && event.code <= VC_KP_9)
		return QString("Num %1").arg(event.code - VC_KP_0);
	switch (event.code) {
	case VC_SHIFT_L:
	case VC_SHIFT_R:
		return "Shift";
	case VC_CONTROL_L:
	case VC_CONTROL_R:
		return "Ctrl";
	case VC_ALT_L:
	case VC_ALT_R:
		return "Opt";
	case VC_META_L:
	case VC_META_R:
		return "Cmd";
	case VC_SPACE:
		return "Space";
	case VC_ENTER:
		return "Return";
	case VC_ESCAPE:
		return "Esc";
	case VC_BACKSPACE:
		return "Del";
	case VC_TAB:
		return "Tab";
	case VC_CAPS_LOCK:
		return "Caps";
	case VC_MINUS:
		return "-";
	case VC_EQUALS:
		return "=";
	case VC_OPEN_BRACKET:
		return "[";
	case VC_CLOSE_BRACKET:
		return "]";
	case VC_BACK_SLASH:
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
		return "Up";
	case VC_DOWN:
		return "Down";
	case VC_LEFT:
		return "Left";
	case VC_RIGHT:
		return "Right";
	case VC_HOME:
		return "Home";
	case VC_END:
		return "End";
	case VC_PAGE_UP:
		return "PgUp";
	case VC_PAGE_DOWN:
		return "PgDn";
	case VC_INSERT:
		return "Insert";
	case VC_DELETE:
		return "Delete";
	case VC_PRINT_SCREEN:
		return "PrtSc";
	case VC_SCROLL_LOCK:
		return "Scroll";
	case VC_PAUSE:
		return "Pause";
	case VC_NUM_LOCK:
		return "NumLock";
	case VC_KP_DIVIDE:
		return "Num /";
	case VC_KP_MULTIPLY:
		return "Num *";
	case VC_KP_SUBTRACT:
		return "Num -";
	case VC_KP_ADD:
		return "Num +";
	case VC_KP_DECIMAL:
		return "Num .";
	case VC_KP_ENTER:
		return "Num Enter";
	default:
		return QString("Unknown key");
	}
}

bool is_alphanumeric_key(const input_data::trace_event &event)
{
	return ((event.keychar >= 'A' && event.keychar <= 'Z') || (event.keychar >= 'a' && event.keychar <= 'z') ||
		(event.keychar >= '0' && event.keychar <= '9')) ||
	       (event.code >= VC_A && event.code <= VC_Z) || (event.code >= VC_0 && event.code <= VC_9) ||
	       (event.code >= VC_KP_0 && event.code <= VC_KP_9);
}
