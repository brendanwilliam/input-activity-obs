
class live_keys_source final : public activity_source {
public:
	enum class fade_curve { linear, ease_in, ease_out, ease_in_out };

	live_keys_source(obs_source_t *source, obs_data_t *settings, bool register_hotkeys = true,
			 bool initial_update = true)
		: activity_source(source, settings, register_hotkeys, initial_update)
	{
	}
	void update(obs_data_t *settings) override
	{
		activity_source::update(settings);
		migrate_legacy_colors(settings, "live_keys.colors_with_alpha", {"live_keys.color"});
		maximum = std::max(1, static_cast<int>(obs_data_get_int(settings, "live_keys.maximum")));
		most_used_maximum = std::max(1, static_cast<int>(obs_data_get_int(settings, "live_keys.top_n")));
		live_row_height = std::max(24, static_cast<int>(obs_data_get_int(settings, "live_keys.row_height")));
		chart_alignment = std::string(obs_data_get_string(settings, "live_keys.top_n_alignment")) == "right"
					  ? Qt::AlignRight
					  : Qt::AlignLeft;
		right_aligned = std::string(obs_data_get_string(settings, "activity.alignment")) == "right";
		const char *legacy_spacing = "live_keys.element_spacing";
		const char *legacy_live_spacing = obs_data_has_user_value(settings, "live_keys.live_element_spacing")
							  ? "live_keys.live_element_spacing"
							  : legacy_spacing;
		const char *horizontal_spacing = obs_data_has_user_value(settings, "live_keys.horizontal_spacing")
							 ? "live_keys.horizontal_spacing"
							 : legacy_live_spacing;
		const char *vertical_spacing = obs_data_has_user_value(settings, "live_keys.vertical_spacing")
						       ? "live_keys.vertical_spacing"
						       : legacy_live_spacing;
		const char *most_used_spacing = obs_data_has_user_value(settings, "live_keys.most_used_element_spacing")
							? "live_keys.most_used_element_spacing"
							: legacy_spacing;
		const int shared_within =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "activity.within_spacing")), 0, 200);
		const int shared_between =
			std::clamp(static_cast<int>(obs_data_get_int(settings, "activity.group_spacing")), 0, 200);
		live_horizontal_spacing =
			obs_data_has_user_value(settings, horizontal_spacing)
				? std::clamp(static_cast<int>(obs_data_get_int(settings, horizontal_spacing)), 0, 200)
				: shared_within;
		live_vertical_spacing =
			obs_data_has_user_value(settings, vertical_spacing)
				? std::clamp(static_cast<int>(obs_data_get_int(settings, vertical_spacing)), -100, 200)
				: shared_within;
		most_used_element_spacing =
			obs_data_has_user_value(settings, most_used_spacing)
				? std::clamp(static_cast<int>(obs_data_get_int(settings, most_used_spacing)), 0, 200)
				: shared_within;
		group_spacing =
			obs_data_has_user_value(settings, "live_keys.group_spacing")
				? std::clamp(static_cast<int>(obs_data_get_int(settings, "live_keys.group_spacing")), 0,
					     200)
				: shared_between;
		show_most_used = obs_data_get_bool(settings, "live_keys.show_most_used");
		chart_first = std::string(obs_data_get_string(settings, "live_keys.layout_order")) == "chart_first";
		live_title = QString::fromUtf8(obs_data_get_string(settings, "live_keys.live_title"));
		show_live_title = obs_data_get_bool(settings, "live_keys.show_live_title");
		live_title_font_size =
			std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.live_title_font_size")));
		most_used_title = QString::fromUtf8(obs_data_get_string(settings, "live_keys.most_used_title"));
		show_most_used_title = obs_data_get_bool(settings, "live_keys.show_most_used_title");
		most_used_title_font_size = std::max(
			8, static_cast<int>(obs_data_get_int(settings, "live_keys.most_used_title_font_size")));
		key_font_size = std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.key_font_size")));
		special_key_font_size =
			std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.special_key_font_size")));
		total_font_size =
			std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.total_font_size")));
		most_used_font_size =
			std::max(8, static_cast<int>(obs_data_get_int(settings, "live_keys.most_used_font_size")));
		blacklist = QString::fromUtf8(obs_data_get_string(settings, "live_keys.blacklist"))
				    .trimmed()
				    .toLower()
				    .split(',', Qt::SkipEmptyParts);
		for (QString &key : blacklist)
			key = key.trimmed();
		for (auto it = ordered.begin(); it != ordered.end();) {
			if (is_blacklisted(it->label)) {
				press_counts.erase(it->code);
				it = ordered.erase(it);
			} else {
				++it;
			}
		}
		for (auto it = press_counts.begin(); it != press_counts.end();) {
			const auto label = key_labels.find(it->first);
			if (label != key_labels.end() && is_blacklisted(label->second))
				it = press_counts.erase(it);
			else
				++it;
		}
		totals_above = std::string(obs_data_get_string(settings, "live_keys.total_position")) != "below";
		fade_duration_ns =
			static_cast<uint64_t>(std::max<int64_t>(0, obs_data_get_int(settings, "live_keys.fade_ms"))) *
			1000 * 1000;
		const std::string curve = obs_data_get_string(settings, "live_keys.fade_curve");
		if (curve == "ease_in")
			fade = fade_curve::ease_in;
		else if (curve == "ease_out")
			fade = fade_curve::ease_out;
		else if (curve == "ease_in_out")
			fade = fade_curve::ease_in_out;
		else
			fade = fade_curve::linear;
		theme_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.inactive_color")));
		pressed_color = obs_color(static_cast<uint32_t>(obs_data_get_int(settings, "activity.active_color")));
	}
	void on_event(const input_data::trace_event &event) override
	{
		if (event.type == EVENT_KEY_PRESSED && !is_blacklisted(key_name(event)) && !held[event.code]) {
			held[event.code] = true;
			key_labels[event.code] = key_name(event);
			ordered.erase(std::remove_if(ordered.begin(), ordered.end(),
						     [&event](const auto &key) { return key.code == event.code; }),
				      ordered.end());
			ordered.push_back({event.code, key_labels[event.code], is_alphanumeric_key(event), 0,
					   ++press_counts[event.code]});
		} else if (event.type == EVENT_KEY_RELEASED) {
			held[event.code] = false;
			for (auto &key : ordered) {
				if (key.code == event.code)
					key.fade_until = event.time_ns + fade_duration_ns;
			}
		}
	}
	void on_snapshot(const input_data::button_map<uint16_t> &keyboard,
			 const input_data::button_map<uint16_t> &) override
	{
		const uint64_t now = os_gettime_ns();
		for (auto it = ordered.begin(); it != ordered.end();) {
			const auto pressed = keyboard.find(it->code);
			if (pressed == keyboard.end() || !pressed->second) {
				held[it->code] = false;
				if (it->fade_until == 0)
					it->fade_until = now + fade_duration_ns;
				if (it->fade_until <= now)
					it = ordered.erase(it);
				else
					++it;
			} else {
				++it;
			}
		}
	}
	void reset_activity() override
	{
		press_counts.clear();
		key_labels.clear();
		for (auto &key : ordered)
			key.press_count = 0;
	}
	void render(QPainter &painter) override
	{
		std::vector<active_key> keys = ordered;
		const int live_title_height =
			show_live_title && !live_title.isEmpty() ? live_title_font_size + padding / 2 : 0;
		const int chart_title_height = show_most_used && show_most_used_title && !most_used_title.isEmpty()
						       ? most_used_title_font_size + padding / 2
						       : 0;
		const int content_height = height - padding * 2;
		const int live_height =
			show_most_used
				? std::min(live_row_height, std::max(1, content_height - live_title_height -
										chart_title_height - group_spacing))
				: std::max(1, content_height - live_title_height);
		const int chart_height = show_most_used ? std::max(1, content_height - live_title_height - live_height -
									      chart_title_height - group_spacing)
							: 0;
		const int chart_top = padding;
		const int live_title_top = chart_first && show_most_used
						   ? chart_top + chart_title_height + chart_height + group_spacing
						   : padding;
		const int live_top = live_title_top + live_title_height;
		if (live_title_height) {
			painter.setFont(font(live_title_font_size));
			draw_text(painter, QRect(padding, live_title_top, width - padding * 2, live_title_height),
				  Qt::AlignLeft | Qt::AlignVCenter, live_title, text_color);
		}
		if (show_most_used && chart_first && chart_title_height) {
			painter.setFont(font(most_used_title_font_size));
			draw_text(painter, QRect(padding, chart_top, width - padding * 2, chart_title_height),
				  Qt::AlignLeft | Qt::AlignVCenter, most_used_title, text_color);
		}
		const int start = std::max(0, static_cast<int>(keys.size()) - maximum);
		const int available_span = width - padding * 2;
		const int horizontal_gap = std::min(live_horizontal_spacing,
						    std::max(0, (available_span - maximum) / std::max(1, maximum - 1)));
		const int key_width = std::max(1, (width - padding * 2 - horizontal_gap * (maximum - 1)) / maximum);
		const int cell_height = live_height;
		const QFont total_font = font(total_font_size);
		const QFontMetrics total_metrics(total_font);
		const int total_height = std::min(total_metrics.height(), std::max(0, cell_height - 9));
		const int vertical_gap = std::min(live_vertical_spacing, std::max(0, cell_height - total_height - 1));
		const int key_height = std::max(1, cell_height - total_height - vertical_gap);
		const uint64_t now = os_gettime_ns();
		for (int index = start; index < static_cast<int>(keys.size()); ++index) {
			const int position = right_aligned ? maximum - 1 - (index - start) : index - start;
			const int x = padding + position * (key_width + horizontal_gap);
			const int y = live_top;
			const QRect row(x, totals_above ? y + total_height + vertical_gap : y, key_width, key_height);
			const auto &key = keys[index];
			const int alpha =
				key.fade_until > now && fade_duration_ns > 0
					? static_cast<int>(255 * fade_alpha(static_cast<double>(key.fade_until - now) /
									    fade_duration_ns))
					: (key.fade_until ? 0 : 255);
			QColor fill = held[key.code] ? pressed_color : theme_color;
			fill.setAlpha(std::clamp(alpha, 0, 255));
			QColor text = text_color;
			text.setAlpha(std::clamp(alpha, 0, 255));
			painter.setBrush(fill);
			painter.setPen(Qt::NoPen);
			painter.drawRoundedRect(row, 6, 6);
			const QString count = QString::number(key.press_count);
			painter.setFont(total_font);
			draw_text(painter,
				  QPoint(x + (key_width - total_metrics.horizontalAdvance(count)) / 2,
					 totals_above ? row.top() - vertical_gap - total_metrics.descent()
						      : row.bottom() + 1 + vertical_gap + total_metrics.ascent()),
				  count, text);
			const QString indicator = key.alphanumeric ? key.label : key_indicator(key.code);
			painter.setFont(font(key_font_size));
			const QString rendered_label = indicator.isEmpty() ? (key.label.size() > 1 ? "?" : key.label)
									   : indicator;
			draw_text(painter, row, Qt::AlignCenter, rendered_label, text);
		}
		if (show_most_used) {
			const int chart_group_top = chart_first ? chart_top : live_top + live_height + group_spacing;
			if (!chart_first && chart_title_height) {
				painter.setFont(font(most_used_title_font_size));
				draw_text(painter,
					  QRect(padding, chart_group_top, width - padding * 2, chart_title_height),
					  Qt::AlignLeft | Qt::AlignVCenter, most_used_title, text_color);
			}
			draw_most_used_chart(painter, QRect(padding, chart_group_top + chart_title_height,
							    std::max(1, width - padding * 2), chart_height));
		}
	}

private:
	bool is_blacklisted(const QString &label) const { return blacklist.contains(label.trimmed().toLower()); }
	void draw_most_used_chart(QPainter &painter, const QRect &bounds) const
	{
		std::vector<active_key> keys;
		for (const auto &[code, count] : press_counts) {
			if (count == 0)
				continue;
			input_data::trace_event event{};
			event.code = code;
			keys.push_back({code, key_labels.count(code) ? key_labels.at(code) : key_name(event),
					is_alphanumeric_key(event), 0, count});
		}
		std::sort(keys.begin(), keys.end(), [](const active_key &left, const active_key &right) {
			return left.press_count != right.press_count ? left.press_count > right.press_count
								     : left.code < right.code;
		});
		keys.resize(std::min(keys.size(), static_cast<size_t>(most_used_maximum)));
		if (keys.empty())
			return;
		const uint64_t highest = keys.front().press_count;
		const int gap = std::min(most_used_element_spacing,
					 std::max(0, bounds.height() / static_cast<int>(keys.size() * 3)));
		const int row_height = std::max(1, (bounds.height() - gap * (static_cast<int>(keys.size()) - 1)) /
							   static_cast<int>(keys.size()));
		const QFontMetrics metrics(font(most_used_font_size));
		const int value_height = std::min(metrics.height(), row_height / 2);
		const int chart_width = bounds.width();
		const bool right_aligned = chart_alignment == Qt::AlignRight;
		const int chart_left = bounds.left();
		for (size_t index = 0; index < keys.size(); ++index) {
			const int top = bounds.top() + static_cast<int>(index) * (row_height + gap);
			const int bar_width =
				std::max(1, static_cast<int>(chart_width * keys[index].press_count / highest));
			const QRect bar(right_aligned ? chart_left + chart_width - bar_width : chart_left,
					top + value_height, bar_width, std::max(1, row_height - value_height));
			painter.setPen(Qt::NoPen);
			const auto pressed = held.find(keys[index].code);
			painter.setBrush(pressed != held.end() && pressed->second ? pressed_color : theme_color);
			painter.drawRoundedRect(bar, 3, 3);
			painter.setFont(font(most_used_font_size));
			draw_text(painter, QRect(chart_left, top, chart_width, value_height),
				  Qt::AlignLeft | Qt::AlignVCenter, keys[index].label, text_color);
			painter.setFont(font(most_used_font_size));
			draw_text(painter, QRect(chart_left, top, chart_width, value_height),
				  Qt::AlignRight | Qt::AlignVCenter, QString::number(keys[index].press_count),
				  text_color);
		}
	}
	double fade_alpha(double remaining) const
	{
		remaining = std::clamp(remaining, 0.0, 1.0);
		switch (fade) {
		case fade_curve::ease_in:
			return 1.0 - (1.0 - remaining) * (1.0 - remaining);
		case fade_curve::ease_out:
			return remaining * remaining;
		case fade_curve::ease_in_out: {
			const double elapsed = 1.0 - remaining;
			const double eased_elapsed = elapsed < 0.5 ? 2.0 * elapsed * elapsed
								   : 1.0 - std::pow(-2.0 * elapsed + 2.0, 2.0) / 2.0;
			return 1.0 - eased_elapsed;
		}
		case fade_curve::linear:
			return remaining;
		}
		return remaining;
	}
	struct active_key {
		uint16_t code;
		QString label;
		bool alphanumeric;
		uint64_t fade_until;
		uint64_t press_count;
	};
	int maximum = 8;
	int most_used_maximum = 8;
	int live_row_height = 96;
	int live_title_font_size = 28, most_used_title_font_size = 28;
	int live_horizontal_spacing = 2, live_vertical_spacing = 2, most_used_element_spacing = 2, group_spacing = 10;
	int key_font_size = 36;
	int special_key_font_size = 28;
	int total_font_size = 24;
	int most_used_font_size = 24;
	uint64_t fade_duration_ns = 300ULL * 1000 * 1000;
	fade_curve fade{fade_curve::linear};
	QColor theme_color{37, 99, 235};
	QColor pressed_color{239, 68, 68};
	std::unordered_map<uint16_t, bool> held;
	std::unordered_map<uint16_t, uint64_t> press_counts;
	std::unordered_map<uint16_t, QString> key_labels;
	std::vector<active_key> ordered;
	bool show_most_used{}, chart_first{}, totals_above{true};
	bool show_live_title{true}, show_most_used_title{true}, right_aligned{};
	Qt::Alignment chart_alignment{Qt::AlignLeft};
	QString live_title{"Live Keys"}, most_used_title{"Most Used Keys"};
	QStringList blacklist;
};
