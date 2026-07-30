	struct motion_point {
		uint64_t time_ns;
		size_t hex_index;
	};
	struct hex_bin {
		QPointF center;
		uint64_t value{};
	};
	void resize_heatmap()
	{
		const QRect rect = heatmap_rect();
		if (rect == heatmap_bounds)
			return;
		heatmap_bounds = rect;
		build_hex_lattice();
		last_motion.reset();
	}
	QRect heatmap_rect() const
	{
		if (tracking_bounds.isEmpty())
			return {padding, padding, 1, 1};
		const int available_width = std::max(1, width - padding * 2);
		const int available_height =
			std::max(1, height - title_content_offset() - padding * 2 - summary_height());
		const double aspect_ratio = static_cast<double>(tracking_bounds.width()) / tracking_bounds.height();
		const int plot_width = std::max(
			1, std::min(available_width, static_cast<int>(std::lround(available_height * aspect_ratio))));
		const int plot_height = std::max(1, std::min(available_height,
							     static_cast<int>(std::lround(plot_width / aspect_ratio))));
		const int x = padding + (available_width - plot_width) / 2;
		const int vertical_space = available_height - plot_height;
		const int y = padding + (summary_above_heatmap ? summary_height() : 0) + vertical_space / 2;
		return {x, y, plot_width, plot_height};
	}
	int information_line_height() const { return QFontMetrics(font()).lineSpacing(); }
	int summary_height() const { return show_distance || show_clicks ? information_line_height() * 2 : 0; }
	void build_hex_lattice()
	{
		hex_bins.clear();
		const QRect rect = heatmap_rect();
		const qreal hex_width = std::sqrt(3.0) * hex_radius;
		const qreal row_step = 1.5 * hex_radius;
		hex_columns = std::max(1, static_cast<int>(std::ceil(rect.width() / hex_width)) + 1);
		hex_rows = std::max(1, static_cast<int>(std::ceil(rect.height() / row_step)) + 1);
		hex_bins.reserve(static_cast<size_t>(hex_columns * hex_rows));
		for (int row = 0; row < hex_rows; ++row) {
			const qreal x_offset = row % 2 ? hex_width / 2.0 : 0.0;
			for (int column = 0; column < hex_columns; ++column)
				hex_bins.push_back({{rect.left() + hex_width / 2.0 + x_offset + column * hex_width,
						     rect.top() + hex_radius + row * row_step}});
		}
	}
	size_t nearest_hex(const QPointF &point) const
	{
		const QRect rect = heatmap_rect();
		const qreal hex_width = std::sqrt(3.0) * hex_radius;
		const qreal row_step = 1.5 * hex_radius;
		const int estimated_row =
			static_cast<int>(std::floor((point.y() - rect.top() - hex_radius) / row_step));
		size_t nearest{};
		qreal nearest_distance = std::numeric_limits<qreal>::max();
		for (int row = std::max(0, estimated_row - 1); row <= std::min(hex_rows - 1, estimated_row + 1);
		     ++row) {
			const qreal x_offset = row % 2 ? hex_width / 2.0 : 0.0;
			const int estimated_column = static_cast<int>(
				std::floor((point.x() - rect.left() - hex_width / 2.0 - x_offset) / hex_width));
			for (int column = std::max(0, estimated_column - 1);
			     column <= std::min(hex_columns - 1, estimated_column + 1); ++column) {
				const size_t index = static_cast<size_t>(row * hex_columns + column);
				const qreal dx = point.x() - hex_bins[index].center.x();
				const qreal dy = point.y() - hex_bins[index].center.y();
				const qreal distance = dx * dx + dy * dy;
				if (distance < nearest_distance) {
					nearest_distance = distance;
					nearest = index;
				}
			}
		}
		return nearest;
	}
	QPainterPath trail_path(size_t first, size_t last) const
	{
		QPainterPath path(trail[first].second);
		if (first == last)
			return path;
		if (first + 1 == last) {
			path.lineTo(trail[last].second);
			return path;
		}
		for (size_t index = first + 1; index < last; ++index) {
			const QPointF midpoint = (trail[index].second + trail[index + 1].second) / 2.0;
			path.quadTo(trail[index].second, midpoint);
		}
		path.lineTo(trail[last].second);
		return path;
	}
	void draw_trail(QPainter &painter, uint64_t now) const
	{
		if (trail.empty())
			return;
		painter.setBrush(Qt::NoBrush);
		for (size_t index = 0; index + 1 < trail.size(); ++index) {
			const double age = std::clamp(
				static_cast<double>(now - trail[index + 1].first) / trail_duration_ns, 0.0, 1.0);
			const double strength = std::pow(1.0 - age, 2.0);
			if (strength <= 0.0)
				continue;
			QPen pen(QColor(active_color.red(), active_color.green(), active_color.blue(),
					static_cast<int>(active_color.alpha() * 180.0 / 255.0 * strength)));
			pen.setWidthF(2.0 + 6.0 * strength);
			pen.setCapStyle(Qt::RoundCap);
			pen.setJoinStyle(Qt::RoundJoin);
			painter.setPen(pen);
			painter.drawPath(trail_path(index, index + 1));
		}
		painter.setBrush(active_color);
		painter.setPen(text_color);
		painter.drawEllipse(trail.back().second, 8, 8);
	}
	void draw_heatmap(QPainter &painter, const QRect &rect) const
	{
		std::vector<uint64_t> visited;
		visited.reserve(hex_bins.size());
		for (const auto &bin : hex_bins)
			if (bin.value)
				visited.push_back(bin.value);
		if (visited.empty())
			return;
		std::sort(visited.begin(), visited.end());
		const uint64_t first_quartile = visited[(visited.size() - 1) / 4];
		const uint64_t second_quartile = visited[(visited.size() - 1) / 2];
		const uint64_t third_quartile = visited[(visited.size() - 1) * 3 / 4];
		painter.save();
		painter.setClipRect(rect);
		for (const auto &bin : hex_bins) {
			if (!bin.value)
				continue;
			const int band = bin.value <= first_quartile    ? 0
					 : bin.value <= second_quartile ? 1
					 : bin.value <= third_quartile  ? 2
									: 3;
			QColor color = heatmap_color(band);
			color.setAlpha(150 * heatmap_opacity / 100);
			painter.setBrush(color);
			QColor outline = heatmap_color(band);
			outline.setAlpha(210 * heatmap_opacity / 100);
			QPen pen(outline, 0.75);
			pen.setJoinStyle(Qt::RoundJoin);
			painter.setPen(pen);
			QPolygonF hexagon;
			for (int corner = 0; corner < 6; ++corner) {
				const qreal angle = (30.0 + corner * 60.0) * M_PI / 180.0;
				hexagon << QPointF(bin.center.x() + hex_radius * std::cos(angle),
						   bin.center.y() + hex_radius * std::sin(angle));
			}
			painter.drawPolygon(hexagon);
		}
		painter.restore();
	}
	QColor heatmap_color(int band) const
	{
		if (heatmap_gradient == "custom") {
			const qreal amount = std::clamp(band / 3.0, 0.0, 1.0);
			const QColor &from = amount <= 0.5 ? custom_gradient_low : custom_gradient_middle;
			const QColor &to = amount <= 0.5 ? custom_gradient_middle : custom_gradient_high;
			const qreal segment_amount = amount <= 0.5 ? amount * 2.0 : (amount - 0.5) * 2.0;
			return {static_cast<int>(std::lround(from.red() + (to.red() - from.red()) * segment_amount)),
				static_cast<int>(
					std::lround(from.green() + (to.green() - from.green()) * segment_amount)),
				static_cast<int>(
					std::lround(from.blue() + (to.blue() - from.blue()) * segment_amount))};
		}
		if (heatmap_gradient == "lime") {
			const QColor colors[] = {{101, 163, 13}, {132, 204, 22}, {190, 242, 100}, {250, 204, 21}};
			return colors[band];
		}
		if (heatmap_gradient == "ocean") {
			const QColor colors[] = {{30, 64, 175}, {14, 116, 144}, {34, 197, 94}, {250, 204, 21}};
			return colors[band];
		}
		const QColor colors[] = {{59, 130, 246}, {6, 182, 212}, {250, 204, 21}, {239, 68, 68}};
		return colors[band];
	}
	void export_heatmap() const
	{
		const QRect rect = heatmap_rect();
		QDir directory(export_directory);
		if (!directory.exists() && !directory.mkpath("."))
			return;
		const QString base_name = QString("input-activity-heatmap-%1")
						  .arg(QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz"));
		const QString path = directory.filePath(base_name + (export_svg ? ".svg" : ".png"));
		if (export_svg) {
			QSvgGenerator generator;
			generator.setFileName(path);
			generator.setSize(rect.size());
			generator.setViewBox(QRect(QPoint(), rect.size()));
			QPainter painter(&generator);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.translate(-rect.topLeft());
			draw_heatmap(painter, rect);
		} else {
			QImage image(rect.size(), QImage::Format_RGBA8888);
			image.fill(Qt::transparent);
			QPainter painter(&image);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.translate(-rect.topLeft());
			draw_heatmap(painter, rect);
			image.save(path, "PNG");
		}
	}
	QRect display_bounds(int selected_display) const
	{
		unsigned char count{};
		std::unique_ptr<screen_data, decltype(&free)> screens(hook_create_screen_info(&count), &free);
		if (!screens || selected_display < 0 || selected_display >= count)
			return {};
		const auto &screen = screens.get()[selected_display];
		return {screen.x, screen.y, screen.width, screen.height};
	}
	void draw_screen_guides(QPainter &painter, const QRect &rect) const
	{
		if (!show_border && !show_center_mark)
			return;
		painter.save();
		QPen pen(text_color, 1.5);
		pen.setCapStyle(Qt::RoundCap);
		painter.setPen(pen);
		painter.setBrush(Qt::NoBrush);
		if (show_border)
			painter.drawRect(rect.adjusted(0, 0, -1, -1));
		if (show_center_mark) {
			const QPointF center = rect.center();
			constexpr qreal crosshair_radius = 8.0;
			painter.drawLine(center - QPointF(crosshair_radius, 0), center + QPointF(crosshair_radius, 0));
			painter.drawLine(center - QPointF(0, crosshair_radius), center + QPointF(0, crosshair_radius));
		}
		painter.restore();
	}
	void draw_summary(QPainter &painter, const QRect &heatmap) const
	{
		const int top = summary_above_heatmap ? padding : heatmap.bottom() + 1;
		const int column_width = std::max(1, (width - padding * 2) / 2);
		const int line_height = information_line_height();
		if (show_distance) {
			const QRect column(padding, top, column_width, summary_height());
			draw_text(painter, QRect(column.left(), top, column.width(), line_height),
				  Qt::AlignLeft | Qt::AlignVCenter, obs_module_text("MouseActivity.Distance"),
				  text_color);
			draw_text(painter, QRect(column.left(), top + line_height, column.width(), line_height),
				  Qt::AlignLeft | Qt::AlignVCenter, distance_label(), text_color);
		}
		if (show_clicks) {
			const QRect column(padding + column_width, top, width - padding * 2 - column_width,
					   summary_height());
			draw_text(painter, QRect(column.left(), top, column.width(), line_height),
				  Qt::AlignRight | Qt::AlignVCenter, obs_module_text("MouseActivity.Clicks"),
				  text_color);
			draw_text(painter, QRect(column.left(), top + line_height, column.width(), line_height),
				  Qt::AlignRight | Qt::AlignVCenter, QString::number(total_clicks), text_color);
		}
	}
	QString distance_label() const
	{
		if (distance_unit == "metric") {
			double value = distance / mouse_dpi * 2.54;
			QString unit = "cm";
			if (value > 10000.0) {
				value /= 100.0;
				unit = "m";
				if (value > 10000.0) {
					value /= 1000.0;
					unit = "km";
				}
			}
			return QString("%1 %2").arg(value, 0, 'f', value < 10.0 ? 2 : 1).arg(unit);
		}
		if (distance_unit == "imperial") {
			double value = distance / mouse_dpi;
			QString unit = "in";
			if (value > 1200.0) {
				value /= 12.0;
				unit = "ft";
				if (value > 5280.0) {
					value /= 5280.0;
					unit = "mi";
				}
			}
			return QString("%1 %2").arg(value, 0, 'f', value < 10.0 ? 2 : 1).arg(unit);
		}
		return QString("%1 px").arg(distance, 0, 'f', 0);
	}
	void draw_pointer(QPainter &painter) const
	{
		if (!coordinates || tracking_bounds.isEmpty())
			return;
		const QRect heatmap = heatmap_rect();
		const QPointF point(heatmap.x() + coordinates->x() * heatmap.width() / tracking_bounds.width(),
				    heatmap.y() + coordinates->y() * heatmap.height() / tracking_bounds.height());
		const auto is_pressed = [&](uint16_t button) {
			const auto found = buttons.find(button);
			return found != buttons.end() && found->second;
		};
		const uint16_t highlighted_button = is_pressed(MOUSE_BUTTON1)   ? MOUSE_BUTTON1
						    : is_pressed(MOUSE_BUTTON2) ? MOUSE_BUTTON2
						    : is_pressed(MOUSE_BUTTON3) ? MOUSE_BUTTON3
										: 0;
		const QColor color = highlighted_button ? button_colors.at(highlighted_button) : active_color;
		QPainterPath pointer;
		pointer.moveTo(point);
		pointer.lineTo(point + QPointF(0, 22));
		pointer.lineTo(point + QPointF(6, 16));
		pointer.lineTo(point + QPointF(11, 27));
		pointer.lineTo(point + QPointF(16, 25));
		pointer.lineTo(point + QPointF(11, 14));
		pointer.lineTo(point + QPointF(19, 14));
		pointer.closeSubpath();
		painter.setBrush(color);
		painter.setPen(QPen(text_color, 1.5));
		painter.drawPath(pointer);
		if (!highlighted_button)
			return;
		const QString &label = highlighted_button == MOUSE_BUTTON1   ? left_label
				       : highlighted_button == MOUSE_BUTTON2 ? right_label
									     : middle_label;
		if (label.isEmpty())
			return;
		const QFontMetrics metrics(painter.font());
		const QRect label_rect(static_cast<int>(point.x()) - metrics.horizontalAdvance(label) - 4,
				       static_cast<int>(point.y()) - metrics.height() - 4,
				       std::max(1, metrics.horizontalAdvance(label) + 2),
				       std::max(1, metrics.height() + 2));
		draw_text(painter, label_rect, Qt::AlignCenter, label, color);
	}
	QString left_label{"L"}, right_label{"R"}, middle_label{"M"};
	QColor active_color{37, 99, 235};
	std::unordered_map<uint16_t, QColor> button_colors{{MOUSE_BUTTON1, {37, 99, 235}},
							   {MOUSE_BUTTON2, {239, 68, 68}},
							   {MOUSE_BUTTON3, {250, 204, 21}}};
	bool show_heatmap{true}, show_live_mouse{true}, show_distance{}, show_clicks{}, summary_above_heatmap{true},
		show_border{}, show_center_mark{}, map_clicks{};
	std::string heatmap_gradient{"spectrum"};
	QColor custom_gradient_low{37, 99, 235};
	QColor custom_gradient_middle{250, 204, 21};
	QColor custom_gradient_high{239, 68, 68};
	uint64_t trail_duration_ns{1500ULL * 1000 * 1000};
	qreal hex_radius{default_heatmap_hex_radius};
	int heatmap_opacity{100};
	QString export_directory;
	bool export_svg{};
	obs_hotkey_id export_hotkey = OBS_INVALID_HOTKEY_ID;
	int display{};
	int64_t mouse_dpi{800};
	QString distance_unit{"pixels"};
	double distance{};
	uint64_t total_clicks{};
	QRect tracking_bounds;
	std::unordered_map<uint16_t, bool> buttons;
	std::deque<std::pair<uint64_t, QPoint>> trail;
	std::optional<QPoint> coordinates;
	std::optional<QPoint> last_distance;
	std::optional<motion_point> last_motion;
	QRect heatmap_bounds;
	int hex_columns{}, hex_rows{};
	std::vector<hex_bin> hex_bins;
};
