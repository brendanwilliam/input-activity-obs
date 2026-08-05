void lol_dashboard_visuals::draw_keys(QPainter &painter, const QRect &bounds, bool right_aligned) const
{
	const QRect section = bounds.adjusted(style_.section_padding, style_.section_padding, -style_.section_padding,
					      -style_.section_padding);
	const QRect content = section.adjusted(style_.element_padding, style_.element_padding, -style_.element_padding,
					       -style_.element_padding);
	if (content.width() < 4 || content.height() < 4)
		return;
	painter.setPen(Qt::white);
	painter.setFont(dashboard_font(style_.labels, QFont::Bold));
	const int active_row_height =
		std::max(style_.button_labels.size + style_.number_labels.size + style_.within_element_gap, 80);
	const int active_title_height = style_.labels.size + style_.within_element_gap;
	const int active_top = content.top();
	const Qt::Alignment edge = right_aligned ? Qt::AlignRight : Qt::AlignLeft;
	lol_dashboard_draw_shadowed_text(painter,
					 QRect(content.left(), active_top, content.width(), active_title_height),
					 edge | Qt::AlignVCenter, obs_module_text("LiveKeys"));
	const int visible = std::min(4, int(active_keys_.size()));
	const int gap = style_.element_x_gap;
	const int key_width = std::max(1, (content.width() - (visible - 1) * gap) / 4);
	const int count_height = style_.number_labels.size + style_.within_element_gap;
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
		painter.setFont(dashboard_font(style_.number_labels));
		lol_dashboard_draw_shadowed_text(
			painter, QRect(x, active_row.top(), key_width, count_height),
			Qt::AlignHCenter | Qt::AlignVCenter,
			QFontMetrics(painter.font()).elidedText(QString::number(key.count), Qt::ElideLeft, key_width));
		painter.setFont(dashboard_font(style_.button_labels, QFont::Bold));
		lol_dashboard_draw_shadowed_text(
			painter, key_rect, Qt::AlignCenter,
			QFontMetrics(painter.font()).elidedText(key.label, Qt::ElideRight, key_rect.width()));
	}
	std::vector<active_key> keys;
	for (const auto &[code, count] : press_counts_)
		keys.push_back({code, lol_dashboard_key_label(code), 0, count});
	std::sort(keys.begin(), keys.end(), [](const auto &a, const auto &b) { return a.count > b.count; });
	if (keys.size() > 8)
		keys.resize(8);
	const uint64_t max =
		keys.empty() ? 1 : std::max_element(keys.begin(), keys.end(), [](const auto &a, const auto &b) {
					   return a.count < b.count;
				   })->count;
	const int chart_title_height = style_.labels.size + style_.within_element_gap;
	painter.setFont(dashboard_font(style_.labels, QFont::Bold));
	const int chart_top = active_top + active_title_height + active_row_height + style_.element_y_gap;
	lol_dashboard_draw_shadowed_text(painter, QRect(content.left(), chart_top, content.width(), chart_title_height),
					 edge | Qt::AlignVCenter,
					 obs_module_text("LoLPerformanceDashboard.MostUsedKeys"));
	const QRect chart(content.left(), chart_top + chart_title_height, content.width(),
			  std::max(1, content.bottom() - chart_top - chart_title_height + 1));
	const int row_height = std::max(1, chart.height() / 8);
	for (int index = 0; index < int(keys.size()); ++index) {
		const int y = chart.top() + index * row_height;
		const int text_height = std::min(style_.number_labels.size, std::max(1, row_height - 8));
		const int within_element_gap = style_.within_element_gap;
		const int bar = std::max(1, int(chart.width() * keys[index].count / max));
		const int bar_left = right_aligned ? chart.right() - bar + 1 : chart.left();
		const QRect bar_rect(bar_left, y + text_height + 2, bar, 12);
		const auto pressed = held_.find(keys[index].code);
		painter.setBrush(pressed != held_.end() && pressed->second ? theme_.active : theme_.inactive);
		painter.setPen(Qt::NoPen);
		painter.drawRoundedRect(bar_rect, 3, 3);
		painter.setPen(Qt::white);
		auto chart_font = style_.number_labels;
		chart_font.size = std::max(10, text_height);
		painter.setFont(dashboard_font(chart_font));
		const QFontMetrics metrics(painter.font());
		const QString count = QString::number(keys[index].count);
		const int count_width = metrics.horizontalAdvance(count);
		const bool count_outside =
			metrics.horizontalAdvance(keys[index].label) + within_element_gap + count_width > bar;
		const int label_width =
			count_outside ? std::max(0, std::min(metrics.horizontalAdvance(keys[index].label),
							     chart.width() - within_element_gap - count_width))
				      : std::max(0, bar - within_element_gap - count_width);
		const QRect label_rect(right_aligned ? bar_rect.right() - label_width + 1 : bar_rect.left(), y,
				       label_width, text_height);
		const QRect count_rect =
			count_outside
				? (right_aligned ? QRect(bar_rect.right() - label_width - within_element_gap -
								 count_width + 1,
							 y, count_width, text_height)
						 : QRect(bar_rect.left() + label_width + within_element_gap, y,
							 count_width, text_height))
				: (right_aligned
					   ? QRect(bar_rect.left(), y, count_width, text_height)
					   : QRect(bar_rect.right() - count_width + 1, y, count_width, text_height));
		if (label_width > 0)
			lol_dashboard_draw_shadowed_text(
				painter, label_rect,
				(right_aligned ? Qt::AlignRight : Qt::AlignLeft) | Qt::AlignVCenter,
				metrics.elidedText(keys[index].label, Qt::ElideRight, label_rect.width()));
		lol_dashboard_draw_shadowed_text(painter, count_rect,
						 (right_aligned ? Qt::AlignLeft : Qt::AlignRight) | Qt::AlignVCenter,
						 metrics.elidedText(count, Qt::ElideLeft, count_rect.width()));
	}
}
