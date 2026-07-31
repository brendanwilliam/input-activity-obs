
class mouse_activity_source final : public activity_source {
public:
	mouse_activity_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			      bool initial_update = true)
		: activity_source(source, settings, register_hotkeys, initial_update)
	{
		if (register_hotkeys)
			export_hotkey = obs_hotkey_register_source(
				source, "export_mouse_heatmap", obs_module_text("MouseActivity.ExportHotkey"),
				[](void *data, obs_hotkey_id, obs_hotkey_t *, bool pressed) {
					if (pressed)
						static_cast<mouse_activity_source *>(data)->export_heatmap();
				},
				this);
	}
	~mouse_activity_source() override { obs_hotkey_unregister(export_hotkey); }
	void update(obs_data_t *settings) override
	{
		const QRect previous_heatmap = heatmap_rect();
		const qreal previous_hex_radius = hex_radius;
		activity_source::update(settings);
		migrate_legacy_colors(settings, "mouse_activity.colors_with_alpha",
				      {"mouse_activity.color", "mouse_activity.left_color",
				       "mouse_activity.right_color", "mouse_activity.middle_color"});
		left_label = QString::fromUtf8(obs_data_get_string(settings, "mouse_activity.left_label"));
		right_label = QString::fromUtf8(obs_data_get_string(settings, "mouse_activity.right_label"));
		middle_label = QString::fromUtf8(obs_data_get_string(settings, "mouse_activity.middle_label"));
		show_heatmap = obs_data_get_bool(settings, "mouse_activity.show_heatmap");
		show_live_mouse = obs_data_get_bool(settings, "mouse_activity.show_live_mouse");
		show_distance = obs_data_get_bool(settings, "mouse_activity.show_distance");
		show_clicks = obs_data_get_bool(settings, "mouse_activity.show_clicks");
		summary_above_heatmap = std::string(obs_data_get_string(settings, "mouse_activity.summary_position")) !=
					"bottom";
		distance_unit = obs_data_get_string(settings, "mouse_activity.distance_unit");
		if (distance_unit != "metric" && distance_unit != "imperial")
			distance_unit = "pixels";
		mouse_dpi = std::max<int64_t>(1, obs_data_get_int(settings, "mouse_activity.mouse_dpi"));
		heatmap_gradient = obs_data_get_string(settings, "mouse_activity.heatmap_gradient");
		custom_gradient_low = obs_color(
			static_cast<uint32_t>(obs_data_get_int(settings, "mouse_activity.custom_gradient_low")));
		custom_gradient_middle = obs_color(
			static_cast<uint32_t>(obs_data_get_int(settings, "mouse_activity.custom_gradient_middle")));
		custom_gradient_high = obs_color(
			static_cast<uint32_t>(obs_data_get_int(settings, "mouse_activity.custom_gradient_high")));
		if (heatmap_gradient == "theme") {
			custom_gradient_low =
				obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.inactive_color")));
			custom_gradient_middle =
				obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.active_color")));
			custom_gradient_high = custom_gradient_middle;
		}
		hex_radius = std::clamp(static_cast<qreal>(obs_data_get_int(settings, "mouse_activity.hex_size")), 1.0,
					100.0);
		heatmap_opacity =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "mouse_activity.opacity")), 0, 100);
		export_directory = QString::fromUtf8(obs_data_get_string(settings, "mouse_activity.export_directory"));
		export_svg = std::string(obs_data_get_string(settings, "mouse_activity.export_format")) == "svg";
		trail_duration_ns = static_cast<uint64_t>(
			std::max<int64_t>(100, obs_data_get_int(settings, "mouse_activity.trail_ms")) * 1000 * 1000);
		active_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.active_color")));
		button_colors[MOUSE_BUTTON1] = active_color;
		button_colors[MOUSE_BUTTON2] = active_color;
		button_colors[MOUSE_BUTTON3] = active_color;
		const bool new_map_clicks = std::string(obs_data_get_string(settings, "mouse_activity.map")) ==
					    "clicks";
		const bool custom_region = obs_data_get_bool(settings, "activity.target.display_rectangle");
		const int new_display = static_cast<int>(obs_data_get_int(settings, "activity.target.display"));
		QRect new_tracking_bounds;
		if (custom_region) {
			const int left = static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.left"));
			const int top = static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.top"));
			const int right =
				static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.right"));
			const int bottom =
				static_cast<int>(obs_data_get_int(settings, "activity.target.rectangle.bottom"));
			if (left <= right && top <= bottom)
				new_tracking_bounds = QRect(left, top, right - left + 1, bottom - top + 1);
		} else {
			new_tracking_bounds = display_bounds(new_display);
		}
		const bool region_changed = new_tracking_bounds != tracking_bounds;
		if (region_changed || new_map_clicks != map_clicks) {
			coordinates.reset();
			clear();
		}
		map_clicks = new_map_clicks;
		display = new_display;
		tracking_bounds = new_tracking_bounds;
		show_border = obs_data_get_bool(settings, "mouse_activity.show_border");
		show_center_mark = obs_data_get_bool(settings, "mouse_activity.show_center_mark");
		if (hex_radius != previous_hex_radius)
			heatmap_bounds = {};
		resize_heatmap();
		if (region_changed || heatmap_rect() != previous_heatmap || hex_radius != previous_hex_radius)
			trail.clear();
	}
	void clear()
	{
		for (auto &bin : hex_bins)
			bin.value = 0;
		last_motion.reset();
	}
	void export_current_heatmap() const { export_heatmap(); }
	void reset_activity() override
	{
		clear();
		trail.clear();
		distance = 0;
		total_clicks = 0;
		last_distance.reset();
	}
	void on_event(const input_data::trace_event &event) override
	{
		bool clicked{};
		if (event.type == EVENT_MOUSE_PRESSED || event.type == EVENT_MOUSE_RELEASED) {
			const bool was_pressed = buttons[event.code];
			buttons[event.code] = event.type == EVENT_MOUSE_PRESSED;
			clicked = event.type == EVENT_MOUSE_PRESSED && !was_pressed && event.code >= MOUSE_BUTTON1 &&
				  event.code <= MOUSE_BUTTON3;
		}
		const bool moved = event.type == EVENT_MOUSE_MOVED || event.type == EVENT_MOUSE_DRAGGED;
		if ((!moved && !clicked) || tracking_bounds.isEmpty())
			return;
		if (!tracking_bounds.contains(QPoint(event.x, event.y))) {
			coordinates.reset();
			last_motion.reset();
			last_distance.reset();
			return;
		}
		if (clicked)
			++total_clicks;
		const int relative_x = event.x - tracking_bounds.x();
		const int relative_y = event.y - tracking_bounds.y();
		coordinates = QPoint(relative_x, relative_y);
		const QRect heatmap = heatmap_rect();
		const QPoint point(heatmap.x() + relative_x * heatmap.width() / tracking_bounds.width(),
				   heatmap.y() + relative_y * heatmap.height() / tracking_bounds.height());
		if (moved) {
			if (last_distance)
				distance +=
					std::hypot(relative_x - last_distance->x(), relative_y - last_distance->y());
			last_distance = QPoint(relative_x, relative_y);
			const qreal maximum_trail_jump = std::hypot(heatmap.width(), heatmap.height()) / 3.0;
			if (!trail.empty() && std::hypot(point.x() - trail.back().second.x(),
							 point.y() - trail.back().second.y()) > maximum_trail_jump)
				trail.clear();
			trail.push_back({event.time_ns, point});
		}
		const size_t hex_index = nearest_hex(point);
		if (map_clicks && clicked) {
			++hex_bins[hex_index].value;
		} else if (!map_clicks && moved && last_motion && event.time_ns > last_motion->time_ns) {
			const uint64_t duration = std::min(event.time_ns - last_motion->time_ns, max_heatmap_gap_ns);
			hex_bins[last_motion->hex_index].value += duration;
		}
		if (moved)
			last_motion = motion_point{event.time_ns, hex_index};
	}
	void on_snapshot(const input_data::button_map<uint16_t> &,
			 const input_data::button_map<uint16_t> &mouse) override
	{
		for (uint16_t button = MOUSE_BUTTON1; button <= MOUSE_BUTTON3; ++button) {
			const auto pressed = mouse.find(button);
			buttons[button] = pressed != mouse.end() && pressed->second;
		}
	}
	void tick(float seconds) override
	{
		activity_source::tick(seconds);
		const uint64_t now = os_gettime_ns();
		while (!trail.empty() && now - trail.front().first > trail_duration_ns)
			trail.pop_front();
	}
	void render(QPainter &painter) override
	{
		const QRect heatmap = heatmap_rect();
		painter.setFont(font());
		if (show_distance || show_clicks)
			draw_summary(painter, heatmap);
		if (show_heatmap)
			draw_heatmap(painter, heatmap);
		if (show_live_mouse)
			draw_trail(painter, os_gettime_ns());
		draw_screen_guides(painter, heatmap);
		if (show_live_mouse)
			draw_pointer(painter);
	}

private:
