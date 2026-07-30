	void for_each_matching(Predicate predicate, double amount = 1.0, uint16_t key_code = 0)
	{
		for (size_t index = 0; index < rows.size(); ++index)
			if (rows[index].enabled && predicate(rows[index]) &&
			    (key_code == 0 || rows[index].metric != intensity_metric::keyboard ||
			     matches_key_scope(rows[index], key_code)))
				current[index] += amount;
	}
	static bool matches_key_scope(const intensity_row &row, uint16_t key_code)
	{
		switch (row.key_scope) {
		case intensity_key_scope::all:
			return true;
		case intensity_key_scope::letters:
			return key_code >= VC_A && key_code <= VC_Z;
		case intensity_key_scope::numbers:
			return key_code >= VC_0 && key_code <= VC_9;
		case intensity_key_scope::list:
			return key_list_contains(row.key_list, key_code);
		}
		return false;
	}
	static bool key_list_contains(const QString &list, uint16_t key_code)
	{
		for (QString token : list.split(',', Qt::SkipEmptyParts)) {
			token = token.trimmed().toLower();
			if ((token.size() == 1 && token[0].isLetter() && key_code == VC_A + token[0].unicode() - 'a') ||
			    (token.size() == 1 && token[0].isDigit() && key_code == VC_0 + token[0].unicode() - '0') ||
			    (token == "space" && key_code == VC_SPACE) || (token == "return" && key_code == VC_ENTER) ||
			    (token == "enter" && key_code == VC_ENTER) || (token == "tab" && key_code == VC_TAB) ||
			    (token == "esc" && key_code == VC_ESCAPE) || (token == "escape" && key_code == VC_ESCAPE) ||
			    (token == "delete" && key_code == VC_BACKSPACE) ||
			    (token == "del" && key_code == VC_BACKSPACE))
				return true;
		}
		return false;
	}

	void advance_to(uint64_t now)
	{
		if (now == 0)
			return;
		if (bucket_start_ns == 0) {
			bucket_start_ns = now;
			return;
		}
		if (now <= bucket_start_ns)
			return;
		while (now - bucket_start_ns >= second_ns) {
			samples.push_back(current);
			if (samples.size() > 60)
				samples.pop_front();
			current.fill(0.0);
			bucket_start_ns += second_ns;
		}
	}

	void clear_samples()
	{
		samples.clear();
		current.fill(0.0);
		bucket_start_ns = 0;
		held_keys.clear();
		held_buttons.clear();
		last_motion.reset();
	}

	QString row_label(const intensity_row &row) const
	{
		if (!row.title.isEmpty())
			return row.title;
		switch (row.metric) {
		case intensity_metric::keyboard:
			return obs_module_text("InputIntensity.Metric.Keyboard");
		case intensity_metric::mouse:
			return obs_module_text("InputIntensity.Metric.Mouse");
		case intensity_metric::actions:
			return obs_module_text("InputIntensity.Metric.Actions");
		case intensity_metric::key: {
			input_data::trace_event event{};
			event.code = row.key;
			return QString("%1 (KPM)").arg(key_name(event));
		}
		case intensity_metric::button:
			return QString("%1 %2 (CPM)")
				.arg(obs_module_text("InputIntensity.Metric.MouseButton"))
				.arg(row.button);
		case intensity_metric::velocity:
			return QString("%1 (%2/s)")
				.arg(obs_module_text("InputIntensity.Metric.Velocity"))
				.arg(velocity_unit == "metric"     ? "cm"
				     : velocity_unit == "imperial" ? "in"
								   : "px");
		}
		return {};
	}

	void draw_box_plot(QPainter &painter, const QRect &rect, const QRect &value_label_rect, size_t row_index) const
	{
		std::vector<double> values;
		const size_t first = samples.size() > static_cast<size_t>(window_seconds)
					     ? samples.size() - static_cast<size_t>(window_seconds)
					     : 0;
		values.reserve(std::max<size_t>(1, samples.size() - first));
		for (size_t index = first; index < samples.size(); ++index)
			values.push_back(display_value(row_index, samples[index][row_index]));
		if (values.empty())
			values.push_back(0.0);
		std::sort(values.begin(), values.end());
		const double minimum = values.front();
		const double maximum = values.back();
		const double first_quartile = values[(values.size() - 1) / 4];
		const double median = values[(values.size() - 1) / 2];
		const double third_quartile = values[(values.size() - 1) * 3 / 4];
		const double current_value = display_value(row_index, current_rate(row_index));
		const double scale_minimum = std::min(minimum, current_value);
		const double scale_maximum = std::max(maximum, current_value);
		const double range = scale_maximum - scale_minimum;
		const auto position = [&](double value) {
			if (range == 0.0)
				return rect.center().x();
			return rect.left() +
			       static_cast<int>(std::lround((value - scale_minimum) / range * (rect.width() - 1)));
		};
		const int min_x = position(minimum);
		const int max_x = position(maximum);
		const int q1_x = position(first_quartile);
		const int q3_x = position(third_quartile);
		const int median_x = position(median);
		const int current_x = position(current_value);
		const int center_y = rect.center().y();
		const int box_height = std::max(6, rect.height() / 2);
		const QRect box(std::min(q1_x, q3_x), center_y - box_height / 2, std::max(1, std::abs(q3_x - q1_x)),
				box_height);

		QPen whisker(text_color, 1.0);
		painter.setPen(whisker);
		painter.setBrush(Qt::NoBrush);
		painter.drawLine(min_x, center_y, max_x, center_y);
		painter.drawLine(min_x, center_y - 3, min_x, center_y + 3);
		painter.drawLine(max_x, center_y - 3, max_x, center_y + 3);
		QColor fill = theme_color;
		fill.setAlpha(150);
		painter.setBrush(fill);
		painter.drawRect(box);
		QPen median_pen(text_color, 2.0);
		painter.setPen(median_pen);
		painter.drawLine(median_x, box.top(), median_x, box.bottom());

		QPen current_pen(pressed_color, 2.0);
		painter.setPen(current_pen);
		painter.drawLine(current_x, rect.top(), current_x, rect.bottom());
		painter.setBrush(pressed_color);
		painter.drawEllipse(QPoint(current_x, center_y), 3, 3);

		const QString min_label = number_label(minimum);
		const QString max_label = number_label(maximum);
		draw_text(painter, value_label_rect, Qt::AlignLeft | Qt::AlignVCenter, min_label, text_color);
		draw_text(painter, value_label_rect, Qt::AlignRight | Qt::AlignVCenter, max_label, text_color);
	}

	double current_rate(size_t row_index) const
	{
		if (bucket_start_ns == 0)
			return 0.0;
		const uint64_t now = os_gettime_ns();
		if (now <= bucket_start_ns)
			return 0.0;
		const uint64_t elapsed_ns = std::min(second_ns, now - bucket_start_ns);
		if (elapsed_ns == 0)
			return 0.0;
		return current[row_index] * static_cast<double>(second_ns) / static_cast<double>(elapsed_ns);
	}

	double display_value(size_t row_index, double value) const
	{
		if (rows[row_index].metric != intensity_metric::velocity)
			return value * 60.0;
		if (velocity_unit == "metric")
			return value / mouse_dpi * 2.54;
		if (velocity_unit == "imperial")
			return value / mouse_dpi;
		return value;
	}

	static QString number_label(double value) { return QString::number(value, 'f', value < 10.0 ? 1 : 0); }

	static constexpr uint64_t second_ns = 1000ULL * 1000 * 1000;
	int window_seconds{30};
	std::array<intensity_row, intensity_max_fields> rows{};
	bool horizontal_layout{};
	std::deque<sample> samples;
	sample current{};
	std::unordered_map<uint16_t, bool> held_keys, held_buttons;
	std::optional<input_data::trace_event> last_motion;
	QColor theme_color{37, 99, 235};
	QColor pressed_color{239, 68, 68};
	int element_spacing{};
	std::string velocity_unit{"pixels"};
	int64_t mouse_dpi{800};
	uint64_t bucket_start_ns{};
	bool configured{};
};

