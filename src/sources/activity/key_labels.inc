
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

QString key_indicator(uint16_t code)
{
	if (code >= VC_F1 && code <= VC_F20)
		return QString(QChar(0x2460 + code - VC_F1));
	if (code >= VC_F21 && code <= VC_F24)
		return QString(QChar(0x3251 + code - VC_F21));
	if (code >= VC_KP_0 && code <= VC_KP_9)
		return QString::number(code - VC_KP_0);

	switch (code) {
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
	case VC_SPACE:
		return "␣";
	case VC_ENTER:
	case VC_KP_ENTER:
		return "↵";
	case VC_ESCAPE:
		return "⎋";
	case VC_BACKSPACE:
		return "⌫";
	case VC_TAB:
		return "⇥";
	case VC_CAPS_LOCK:
		return "⇪";
	case VC_UP:
		return "↑";
	case VC_DOWN:
		return "↓";
	case VC_LEFT:
		return "←";
	case VC_RIGHT:
		return "→";
	case VC_HOME:
		return "↖";
	case VC_END:
		return "↘";
	case VC_PAGE_UP:
		return "⇞";
	case VC_PAGE_DOWN:
		return "⇟";
	case VC_INSERT:
		return "▣";
	case VC_DELETE:
		return "⌦";
	case VC_PRINT_SCREEN:
		return "▧";
	case VC_SCROLL_LOCK:
		return "◉";
	case VC_PAUSE:
		return "⏸";
	case VC_NUM_LOCK:
		return "⇭";
	case VC_KP_DIVIDE:
		return "÷";
	case VC_KP_MULTIPLY:
		return "×";
	case VC_KP_SUBTRACT:
		return "−";
	case VC_KP_ADD:
		return "+";
	case VC_KP_DECIMAL:
		return ".";
	default:
		return {};
	}
}

bool draw_modifier_indicator(QPainter &painter, const QRect &bounds, uint16_t code, const QColor &color)
{
	const QRectF icon =
		bounds.adjusted(bounds.width() / 4, bounds.height() / 4, -bounds.width() / 4, -bounds.height() / 4);
	const QPointF center = icon.center();
	const qreal stroke_width = std::max<qreal>(2.0, icon.height() / 10.0);
	const auto draw_path = [&](const QPainterPath &path, bool filled = false) {
		QPen shadow(QColor(0, 0, 0, color.alpha()), stroke_width);
		painter.setBrush(filled ? QBrush(QColor(0, 0, 0, color.alpha())) : QBrush(Qt::NoBrush));
		painter.setPen(filled ? Qt::NoPen : shadow);
		painter.drawPath(path.translated(2, 2));
		painter.setBrush(filled ? QBrush(color) : QBrush(Qt::NoBrush));
		painter.setPen(filled ? Qt::NoPen : QPen(color, stroke_width));
		painter.drawPath(path);
	};

	QPainterPath path;
	switch (code) {
	case VC_SHIFT_L:
	case VC_SHIFT_R:
		path.moveTo(center.x(), icon.top());
		path.lineTo(icon.right(), center.y());
		path.lineTo(center.x() + icon.width() / 5, center.y());
		path.lineTo(center.x() + icon.width() / 5, icon.bottom());
		path.lineTo(center.x() - icon.width() / 5, icon.bottom());
		path.lineTo(center.x() - icon.width() / 5, center.y());
		path.lineTo(icon.left(), center.y());
		path.closeSubpath();
		draw_path(path, true);
		return true;
	case VC_CONTROL_L:
	case VC_CONTROL_R:
		path.moveTo(icon.left(), icon.bottom() - icon.height() / 4);
		path.lineTo(center.x(), icon.top() + icon.height() / 4);
		path.lineTo(icon.right(), icon.bottom() - icon.height() / 4);
		draw_path(path);
		return true;
	case VC_ALT_L:
	case VC_ALT_R:
		path.addEllipse(QRectF(icon.left(), center.y() - stroke_width, stroke_width * 2, stroke_width * 2));
		path.moveTo(icon.left() + stroke_width * 2, center.y());
		path.lineTo(icon.right(), center.y());
		path.moveTo(icon.right() - icon.width() / 3, center.y());
		path.lineTo(icon.right(), icon.top() + icon.height() / 4);
		draw_path(path);
		return true;
	case VC_META_L:
	case VC_META_R:
#if defined(__APPLE__)
		for (const QPointF &offset : {QPointF(-1, -1), QPointF(1, -1), QPointF(-1, 1), QPointF(1, 1)}) {
			const QPointF point(center.x() + offset.x() * icon.width() / 4,
					    center.y() + offset.y() * icon.height() / 4);
			path.addEllipse(QRectF(point.x() - icon.width() / 7, point.y() - icon.height() / 7,
					       icon.width() * 2 / 7, icon.height() * 2 / 7));
		}
		path.addRect(QRectF(center.x() - icon.width() / 7, center.y() - icon.height() / 7, icon.width() * 2 / 7,
				    icon.height() * 2 / 7));
#else
		path.addRect(QRectF(icon.left(), icon.top(), icon.width() / 2 - stroke_width / 2,
				    icon.height() / 2 - stroke_width / 2));
		path.addRect(QRectF(center.x() + stroke_width / 2, icon.top(), icon.width() / 2 - stroke_width / 2,
				    icon.height() / 2 - stroke_width / 2));
		path.addRect(QRectF(icon.left(), center.y() + stroke_width / 2, icon.width() / 2 - stroke_width / 2,
				    icon.height() / 2 - stroke_width / 2));
		path.addRect(QRectF(center.x() + stroke_width / 2, center.y() + stroke_width / 2,
				    icon.width() / 2 - stroke_width / 2, icon.height() / 2 - stroke_width / 2));
#endif
		draw_path(path);
		return true;
	default:
		return false;
	}
}
