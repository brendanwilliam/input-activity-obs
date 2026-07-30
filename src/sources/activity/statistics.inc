class statistics_source final : public activity_source {
public:
	statistics_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			  bool initial_update = true)
		: activity_source(source, settings, register_hotkeys, initial_update)
	{
	}
	void update(obs_data_t *settings) override
	{
		activity_source::update(settings);
		show_keys = obs_data_get_bool(settings, "statistics.show_keys");
		show_clicks = obs_data_get_bool(settings, "statistics.show_clicks");
		show_actions = obs_data_get_bool(settings, "statistics.show_actions");
		const int legacy_title_font_size =
			obs_data_has_user_value(settings, "statistics.title_font_size")
				? std::clamp(static_cast<int>(obs_data_get_int(settings, "statistics.title_font_size")),
					     8, 200)
				: 28;
		const auto title_size = [&](const char *name) {
			return obs_data_has_user_value(settings, name)
				       ? std::clamp(static_cast<int>(obs_data_get_int(settings, name)), 8, 200)
				       : legacy_title_font_size;
		};
		keys_title_font_size = title_size("statistics.keys_title_font_size");
		clicks_title_font_size = title_size("statistics.clicks_title_font_size");
		actions_title_font_size = title_size("statistics.actions_title_font_size");
		metric_font_size =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "statistics.metric_font_size")), 8, 200);
		const int legacy_spacing =
			obs_data_has_user_value(settings, "statistics.element_spacing")
				? std::clamp(static_cast<int>(obs_data_get_int(settings, "statistics.element_spacing")),
					     0, 200)
				: 10;
		const auto spacing = [&](const char *name) {
			return obs_data_has_user_value(settings, name)
				       ? std::clamp(static_cast<int>(obs_data_get_int(settings, name)), 0, 200)
				       : legacy_spacing;
		};
		horizontal_spacing = obs_data_has_user_value(settings, "statistics.horizontal_spacing")
					     ? spacing("statistics.horizontal_spacing")
					     : static_cast<int>(obs_data_get_int(settings, "activity.within_spacing"));
		vertical_spacing = obs_data_has_user_value(settings, "statistics.vertical_spacing")
					   ? spacing("statistics.vertical_spacing")
					   : static_cast<int>(obs_data_get_int(settings, "activity.within_spacing"));
		group_spacing = obs_data_has_user_value(settings, "statistics.group_spacing")
					? spacing("statistics.group_spacing")
					: static_cast<int>(obs_data_get_int(settings, "activity.group_spacing"));
		right_aligned = std::string(obs_data_get_string(settings, "activity.alignment")) == "right";
		show_lap_keys = obs_data_get_bool(settings, "statistics.show_lap_keys");
		show_lap_clicks = obs_data_get_bool(settings, "statistics.show_lap_clicks");
		show_lap_actions = obs_data_get_bool(settings, "statistics.show_lap_actions");
		theme_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.inactive_color")));
		pressed_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.active_color")));
	}
	void on_event(const input_data::trace_event &event) override
	{
		if (event.type == EVENT_KEY_PRESSED) {
			if (!held_keys[event.code]) {
				held_keys[event.code] = true;
				keys.push_back(event.time_ns);
				++total_keys;
				++lap_keys;
				keys_highlight_until = os_gettime_ns() + statistics_highlight_duration_ns;
			}
		} else if (event.type == EVENT_KEY_RELEASED) {
			held_keys[event.code] = false;
		} else if (event.type == EVENT_MOUSE_PRESSED && event.code >= MOUSE_BUTTON1 &&
			   event.code <= MOUSE_BUTTON3) {
			if (!held_buttons[event.code]) {
				held_buttons[event.code] = true;
				clicks.push_back(event.time_ns);
				++total_clicks;
				++lap_clicks;
				clicks_highlight_until = os_gettime_ns() + statistics_highlight_duration_ns;
			}
		} else if (event.type == EVENT_MOUSE_RELEASED) {
			held_buttons[event.code] = false;
		}
	}
	void on_snapshot(const input_data::button_map<uint16_t> &keyboard,
			 const input_data::button_map<uint16_t> &mouse) override
	{
		held_keys = keyboard;
		for (uint16_t button = MOUSE_BUTTON1; button <= MOUSE_BUTTON3; ++button) {
			const auto pressed = mouse.find(button);
			held_buttons[button] = pressed != mouse.end() && pressed->second;
		}
	}
	void tick(float seconds) override
	{
		activity_source::tick(seconds);
		const uint64_t cutoff = os_gettime_ns() - minute_ns;
		while (!keys.empty() && keys.front() < cutoff)
			keys.pop_front();
		while (!clicks.empty() && clicks.front() < cutoff)
			clicks.pop_front();
	}
	void render(QPainter &painter) override
	{
		struct statistic_metric {
			QString label;
			QString value;
			bool increasing;
		};
		struct statistic_block {
			QString title;
			int title_font_size;
			std::vector<statistic_metric> metrics;
		};
		std::vector<statistic_block> blocks;
		const uint64_t now = os_gettime_ns();
		const bool keys_increasing = now < keys_highlight_until;
		const bool clicks_increasing = now < clicks_highlight_until;
		const bool actions_increasing = keys_increasing || clicks_increasing;

		if (show_keys)
			blocks.push_back({obs_module_text("Statistics.Keys"),
					  keys_title_font_size,
					  {{"KPM: ", QString::number(keys.size()), keys_increasing},
					   {"Total: ", QString::number(total_keys), keys_increasing}}});

		if (show_clicks)
			blocks.push_back({obs_module_text("Statistics.Clicks"),
					  clicks_title_font_size,
					  {{"CPM: ", QString::number(clicks.size()), clicks_increasing},
					   {"Total: ", QString::number(total_clicks), clicks_increasing}}});

		if (show_actions)
			blocks.push_back(
				{obs_module_text("Statistics.Actions"),
				 actions_title_font_size,
				 {{"APM: ", QString::number(keys.size() + clicks.size()), actions_increasing},
				  {"Total: ", QString::number(total_keys + total_clicks), actions_increasing}}});

		std::vector<statistic_metric> lap_metrics;
		if (show_lap_keys)
			lap_metrics.push_back({QString("%1: ").arg(obs_module_text("Statistics.LapKeys")),
					       QString::number(lap_keys), keys_increasing});
		if (show_lap_clicks)
			lap_metrics.push_back({QString("%1: ").arg(obs_module_text("Statistics.LapClicks")),
					       QString::number(lap_clicks), clicks_increasing});
		if (show_lap_actions)
			lap_metrics.push_back({QString("%1: ").arg(obs_module_text("Statistics.LapActions")),
					       QString::number(lap_keys + lap_clicks), actions_increasing});
		if (!lap_metrics.empty())
			blocks.push_back(
				{obs_module_text("Statistics.Lap"), keys_title_font_size, std::move(lap_metrics)});

		const QRect bounds(padding, padding, width - padding * 2, height - padding * 2);
		if (blocks.empty()) {
			painter.setFont(font());
			draw_text(painter, bounds, Qt::AlignCenter, obs_module_text("Statistics.NoMetrics"),
				  text_color);
			return;
		}

		const QFont metric_font = font(metric_font_size);
		const int metric_height = QFontMetrics(metric_font).lineSpacing();
		size_t metric_columns{};
		for (const statistic_block &block : blocks)
			metric_columns = std::max(metric_columns, block.metrics.size());
		std::vector<int> label_widths(metric_columns), value_widths(metric_columns);
		for (const statistic_block &block : blocks)
			for (size_t index = 0; index < block.metrics.size(); ++index) {
				const statistic_metric &metric = block.metrics[index];
				label_widths[index] = std::max(
					label_widths[index], QFontMetrics(metric_font).horizontalAdvance(metric.label));
				value_widths[index] = std::max(
					value_widths[index], QFontMetrics(metric_font).horizontalAdvance(metric.value));
			}
		int grid_width{};
		for (size_t index = 0; index < metric_columns; ++index) {
			grid_width += label_widths[index] + value_widths[index];
			if (index + 1 < metric_columns)
				grid_width += horizontal_spacing;
		}
		int content_height{};
		for (const statistic_block &block : blocks)
			content_height += QFontMetrics(font(block.title_font_size)).lineSpacing() + vertical_spacing +
					  metric_height;
		const int gap =
			std::min(group_spacing, std::max(0, (bounds.height() - content_height) /
								    std::max(1, static_cast<int>(blocks.size()) - 1)));
		const int total_height = content_height + std::max(0, static_cast<int>(blocks.size()) - 1) * gap;
		int top = bounds.center().y() - total_height / 2;
		for (const statistic_block &block : blocks) {
			const QFont title_font = font(block.title_font_size);
			const int title_height = QFontMetrics(title_font).lineSpacing();
			painter.setFont(title_font);
			draw_text(painter, QRect(bounds.left(), top, bounds.width(), title_height),
				  (right_aligned ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter, block.title,
				  text_color);
			top += title_height;
			painter.setFont(metric_font);
			top += vertical_spacing;
			int left = right_aligned ? bounds.right() - grid_width + 1 : bounds.left();
			for (size_t index = 0; index < metric_columns; ++index) {
				if (index < block.metrics.size()) {
					const statistic_metric &metric = block.metrics[index];
					const int label_width =
						QFontMetrics(metric_font).horizontalAdvance(metric.label);
					const int value_width =
						QFontMetrics(metric_font).horizontalAdvance(metric.value);
					draw_text(painter, QRect(left, top, label_width, metric_height),
						  Qt::AlignLeft | Qt::AlignVCenter, metric.label, text_color);
					draw_text(painter,
						  QRect(left + label_widths[index], top, value_width, metric_height),
						  Qt::AlignLeft | Qt::AlignVCenter, metric.value,
						  metric.increasing ? pressed_color : theme_color);
				}
				left += label_widths[index] + value_widths[index] + horizontal_spacing;
			}
			top += metric_height + gap;
		}
	}
	void reset_activity() override
	{
		keys.clear();
		clicks.clear();
		total_keys = 0;
		total_clicks = 0;
		lap_keys = 0;
		lap_clicks = 0;
	}
	void lap_activity() override
	{
		lap_keys = 0;
		lap_clicks = 0;
	}

private:
	std::deque<uint64_t> keys, clicks;
	std::unordered_map<uint16_t, bool> held_keys, held_buttons;
	uint64_t total_keys{}, total_clicks{}, lap_keys{}, lap_clicks{};
	int keys_title_font_size{28}, clicks_title_font_size{28}, actions_title_font_size{28}, metric_font_size{36},
		horizontal_spacing{10}, vertical_spacing{10}, group_spacing{10};
	bool show_keys{true}, show_clicks{true}, show_actions{true}, right_aligned{};
	bool show_lap_keys{}, show_lap_clicks{}, show_lap_actions{};
	QColor theme_color{37, 99, 235};
	QColor pressed_color{239, 68, 68};
	uint64_t keys_highlight_until{}, clicks_highlight_until{};
};
