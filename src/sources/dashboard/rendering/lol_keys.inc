void lol_dashboard_visuals::draw_keys(QPainter &painter, const QRect &bounds, bool right_aligned) const
{
	const QRect content =
		bounds.adjusted(dashboard_padding, dashboard_padding, -dashboard_padding, -dashboard_padding);
	if (content.width() < 4 || content.height() < 4)
		return;
	painter.setPen(Qt::white);
	painter.setFont(display_font(title_size, QFont::Bold));
	constexpr int active_row_height = 80;
	const int active_title_height = title_size + dashboard_padding / 2;
	const int active_top = content.top();
	const Qt::Alignment edge = right_aligned ? Qt::AlignRight : Qt::AlignLeft;
	lol_dashboard_draw_shadowed_text(painter,
					 QRect(content.left(), active_top, content.width(), active_title_height),
					 edge | Qt::AlignVCenter, obs_module_text("LiveKeys"));
	const int visible = std::min(4, int(active_keys_.size()));
	const int gap = 10;
	const int key_width = std::max(1, (content.width() - (visible - 1) * gap) / 4);
	const int count_height = subtitle_size;
	const QRect active_row(content.left(), active_top + active_title_height, content.width(), active_row_height);
	for (int index = 0; index < visible; ++index) {
		const auto &key = active_keys_[active_keys_.size() - visible + index];
		const int x = right_aligned ? active_row.right() - (visible - index) * key_width -
						      (visible - index - 1) * gap + 1
					    : active_row.left() + index * (key_width + gap);
		QColor fill = held_.count(key.code) && held_.at(key.code) ? theme_.active : theme_.inactive;
		painter.setBrush(fill);
		painter.setPen(Qt::NoPen);
		const QRect key_rect(x, active_row.top() + count_height, key_width, active_row.height() - count_height);
		painter.drawRoundedRect(key_rect, 6, 6);
		painter.setPen(Qt::white);
		painter.setFont(data_font(subtitle_size));
		lol_dashboard_draw_shadowed_text(painter, QRect(x, active_row.top(), key_width, count_height),
						 Qt::AlignHCenter | Qt::AlignVCenter, QString::number(key.count));
		painter.setFont(data_font(text_size, QFont::Bold));
		lol_dashboard_draw_shadowed_text(painter, key_rect, Qt::AlignCenter, key.label);
	}
	std::vector<active_key> keys;
	for (const auto &[code, count] : press_counts_)
		keys.push_back({code, key_label(code), 0, count});
	std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.count > b.count; });
	if (keys.size() > 8)
		keys.resize(8);
	const uint64_t max =
		keys.empty() ? 1 : std::max_element(keys.begin(), keys.end(), [](const auto &a, const auto &b) {
					   return a.count < b.count;
				   })->count;
	const int chart_title_height = title_size + dashboard_padding / 2;
	painter.setFont(display_font(title_size, QFont::Bold));
	const int chart_top = active_top + active_title_height + active_row_height + dashboard_padding;
	lol_dashboard_draw_shadowed_text(painter, QRect(content.left(), chart_top, content.width(), chart_title_height),
					 edge | Qt::AlignVCenter,
					 obs_module_text("LoLPerformanceDashboard.MostUsedKeys"));
	const QRect chart(content.left(), chart_top + chart_title_height, content.width(),
			  std::max(1, content.bottom() - chart_top - chart_title_height + 1));
	const int row_height = std::max(1, chart.height() / 8);
	for (int index = 0; index < int(keys.size()); ++index) {
		const int y = chart.top() + index * row_height;
		const int bar = std::max(1, int(chart.width() * keys[index].count / max));
		const int text_height = std::min(subtitle_size, std::max(1, row_height - 8));
		const QRect bar_rect(right_aligned ? chart.right() - bar + 1 : chart.left(), y + text_height + 2, bar,
				     12);
		const auto pressed = held_.find(keys[index].code);
		painter.setBrush(pressed != held_.end() && pressed->second ? theme_.active : theme_.inactive);
		painter.setPen(Qt::NoPen);
		painter.drawRoundedRect(bar_rect, 3, 3);
		painter.setPen(Qt::white);
		painter.setFont(data_font(subtitle_size));
		const QRect label_rect(right_aligned ? chart.right() - chart.width() / 3 + 1 : chart.left(), y,
				       chart.width() / 3, text_height);
		const QRect count_rect(right_aligned ? bar_rect.left() : bar_rect.right() - chart.width() / 3 + 1, y,
				       chart.width() / 3, text_height);
		lol_dashboard_draw_shadowed_text(painter, label_rect,
						 (right_aligned ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
						 keys[index].label);
		lol_dashboard_draw_shadowed_text(painter, count_rect,
						 (right_aligned ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
						 QString::number(keys[index].count));
	}
}
