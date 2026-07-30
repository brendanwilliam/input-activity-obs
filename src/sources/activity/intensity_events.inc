
enum class intensity_metric { keyboard, mouse, actions, key, button, velocity };
enum class intensity_key_scope { all, letters, numbers, list };
constexpr size_t intensity_max_fields = 4;

struct intensity_row {
	bool enabled{};
	intensity_metric metric{intensity_metric::actions};
	uint16_t key{};
	uint16_t button{MOUSE_BUTTON1};
	intensity_key_scope key_scope{intensity_key_scope::all};
	QString title;
	QString key_list;

	bool operator==(const intensity_row &other) const
	{
		return enabled == other.enabled && metric == other.metric && key == other.key &&
		       button == other.button && key_scope == other.key_scope && title == other.title &&
		       key_list == other.key_list;
	}
	bool operator!=(const intensity_row &other) const { return !(*this == other); }
};

class input_intensity_source final : public activity_source {
public:
	using sample = std::array<double, intensity_max_fields>;

	input_intensity_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			       bool initial_update = true)
		: activity_source(source, settings, register_hotkeys, initial_update)
	{
	}

	void update(obs_data_t *settings) override
	{
		activity_source::update(settings);
		migrate_legacy_colors(settings, "input_intensity.colors_with_alpha",
				      {"input_intensity.color", "input_intensity.theme_color",
				       "input_intensity.pressed_color"});
		if (!obs_data_has_user_value(settings, "input_intensity.theme_color") &&
		    obs_data_has_user_value(settings, "input_intensity.color"))
			obs_data_set_int(settings, "input_intensity.theme_color",
					 obs_data_get_int(settings, "input_intensity.color"));

		const int new_window =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "input_intensity.window")), 1, 60);
		element_spacing = std::clamp(
			static_cast<int>(obs_data_get_int(settings, "input_intensity.element_spacing")), 0, 200);
		velocity_unit = obs_data_get_string(settings, "input_intensity.velocity_unit");
		if (velocity_unit != "metric" && velocity_unit != "imperial")
			velocity_unit = "pixels";
		mouse_dpi = std::max<int64_t>(1, obs_data_get_int(settings, "input_intensity.mouse_dpi"));
		std::array<intensity_row, intensity_max_fields> new_rows{};
		for (size_t index = 0; index < new_rows.size(); ++index) {
			const std::string prefix = "input_intensity.row" + std::to_string(index) + ".";
			new_rows[index].enabled = obs_data_get_bool(settings, (prefix + "enabled").c_str());
			const std::string type = obs_data_get_string(settings, (prefix + "metric").c_str());
			if (type == "keyboard")
				new_rows[index].metric = intensity_metric::keyboard;
			else if (type == "mouse")
				new_rows[index].metric = intensity_metric::mouse;
			else if (type == "key")
				new_rows[index].metric = intensity_metric::key;
			else if (type == "button")
				new_rows[index].metric = intensity_metric::button;
			else if (type == "velocity")
				new_rows[index].metric = intensity_metric::velocity;
			else
				new_rows[index].metric = intensity_metric::actions;
			new_rows[index].key =
				static_cast<uint16_t>(obs_data_get_int(settings, (prefix + "key").c_str()));
			new_rows[index].button =
				static_cast<uint16_t>(obs_data_get_int(settings, (prefix + "button").c_str()));
			const std::string scope = obs_data_get_string(settings, (prefix + "key_scope").c_str());
			new_rows[index].key_scope = scope == "letters"   ? intensity_key_scope::letters
						    : scope == "numbers" ? intensity_key_scope::numbers
						    : scope == "list"    ? intensity_key_scope::list
									 : intensity_key_scope::all;
			new_rows[index].title =
				QString::fromUtf8(obs_data_get_string(settings, (prefix + "title").c_str()));
			new_rows[index].key_list =
				QString::fromUtf8(obs_data_get_string(settings, (prefix + "key_list").c_str()));
		}
		const QColor new_theme_color =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.inactive_color")));
		const QColor new_pressed_color =
			obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.active_color")));
		const bool data_changed = configured && (new_window != window_seconds || new_rows != rows);
		window_seconds = new_window;
		rows = new_rows;
		horizontal_layout = std::string(obs_data_get_string(settings, "input_intensity.layout")) == "row";
		theme_color = new_theme_color;
		pressed_color = new_pressed_color;
		if (data_changed)
			clear_samples();
		configured = true;
	}

	void on_event(const input_data::trace_event &event) override
	{
		advance_to(event.time_ns);
		if (event.type == EVENT_KEY_PRESSED) {
			if (!held_keys[event.code]) {
				held_keys[event.code] = true;
				for_each_matching(
					[&](const intensity_row &row) {
						return row.metric == intensity_metric::keyboard ||
						       row.metric == intensity_metric::actions ||
						       (row.metric == intensity_metric::key && row.key == event.code);
					},
					1.0, event.code);
			}
		} else if (event.type == EVENT_KEY_RELEASED) {
			held_keys[event.code] = false;
		} else if (event.type == EVENT_MOUSE_PRESSED && event.code >= MOUSE_BUTTON1 &&
			   event.code <= MOUSE_BUTTON5) {
			if (!held_buttons[event.code]) {
				held_buttons[event.code] = true;
				for_each_matching([&](const intensity_row &row) {
					return row.metric == intensity_metric::mouse ||
					       row.metric == intensity_metric::actions ||
					       (row.metric == intensity_metric::button && row.button == event.code);
				});
			}
		} else if (event.type == EVENT_MOUSE_RELEASED) {
			held_buttons[event.code] = false;
		} else if (event.type == EVENT_MOUSE_MOVED || event.type == EVENT_MOUSE_DRAGGED) {
			if (last_motion) {
				const double distance = std::hypot(static_cast<double>(event.x - last_motion->x),
								   static_cast<double>(event.y - last_motion->y));
				for_each_matching(
					[&](const intensity_row &row) {
						return row.metric == intensity_metric::velocity;
					},
					distance);
			}
			last_motion = event;
		}
	}

	void on_snapshot(const input_data::button_map<uint16_t> &keyboard,
			 const input_data::button_map<uint16_t> &mouse) override
	{
		for (auto &[code, held] : held_keys) {
			const auto pressed = keyboard.find(code);
			held = pressed != keyboard.end() && pressed->second;
		}
		for (auto &[code, held] : held_buttons) {
			const auto pressed = mouse.find(code);
			held = pressed != mouse.end() && pressed->second;
		}
	}

	void tick(float seconds) override
	{
		activity_source::tick(seconds);
		advance_to(os_gettime_ns());
	}

	void reset_activity() override { clear_samples(); }

	void render(QPainter &painter) override
	{
		std::vector<size_t> active_rows;
		for (size_t index = 0; index < rows.size(); ++index)
			if (rows[index].enabled)
				active_rows.push_back(index);
		if (active_rows.empty()) {
			painter.setFont(font());
			draw_text(painter, QRect(padding, padding, width - padding * 2, height - padding * 2),
				  Qt::AlignCenter, obs_module_text("InputIntensity.NoMetrics"), text_color);
			return;
		}

		const QRect bounds(padding, padding, std::max(1, width - padding * 2),
				   std::max(1, height - padding * 2));
		const int available_span = horizontal_layout ? bounds.width() : bounds.height();
		const int gap = std::min(element_spacing,
					 std::max(0, (available_span - static_cast<int>(active_rows.size())) /
							     std::max(1, static_cast<int>(active_rows.size()) - 1)));
		const int total_spacing = gap * std::max(0, static_cast<int>(active_rows.size()) - 1);
		const int item_span =
			std::max(1, (available_span - total_spacing) / static_cast<int>(active_rows.size()));
		const QFont label_font = font(subtitle_size);
		const QFont value_font = font(text_size);
		const int label_text_height = QFontMetrics(label_font).lineSpacing() + 4;
		const int value_text_height = QFontMetrics(value_font).lineSpacing() + 4;

		for (size_t visible_index = 0; visible_index < active_rows.size(); ++visible_index) {
			const size_t row_index = active_rows[visible_index];
			const QRect row_rect =
				horizontal_layout
					? QRect(bounds.left() + static_cast<int>(visible_index) * (item_span + gap),
						bounds.top(), item_span, bounds.height())
					: QRect(bounds.left(),
						bounds.top() + static_cast<int>(visible_index) * (item_span + gap),
						bounds.width(), item_span);
			const int label_height = std::max(1, std::min(row_rect.height() / 3, label_text_height));
			const int value_label_height = std::max(1, std::min(row_rect.height() / 3, value_text_height));
			const QRect label_rect(row_rect.left(), row_rect.top() + 1, row_rect.width(), label_height - 1);
			const QRect chart_rect(row_rect.left(), label_rect.bottom() + 3, row_rect.width(),
					       std::max(1, row_rect.height() - label_height - value_label_height - 4));
			const QRect value_label_rect(row_rect.left(), chart_rect.bottom() + 1, row_rect.width(),
						     value_label_height + 1);
			painter.setFont(label_font);
			draw_text(painter, label_rect, Qt::AlignLeft | Qt::AlignTop, row_label(rows[row_index]),
				  text_color);
			painter.setFont(value_font);
			draw_box_plot(painter, chart_rect, value_label_rect, row_index);
		}
	}

private:
	template<typename Predicate>
