#include "lol_visuals.hpp"

#include <QFont>
#include <QFontDatabase>
#include <QPainter>
#include <QPolygonF>
#include <algorithm>
#include <cmath>
#include <obs-module.h>
#include <util/platform.h>

namespace sources {
namespace {
constexpr uint64_t second_ns = 1000000000ULL;
constexpr uint64_t max_heatmap_gap_ns = 250000000ULL;
constexpr int dashboard_padding = 20;
constexpr int title_size = 22;
constexpr int subtitle_size = 18;
constexpr int text_size = 30;

void ensure_dashboard_fonts_registered()
{
	static const bool registered = [] {
		for (const char *resource : {"fonts/ScienceGothic.ttf", "fonts/Inter.ttf"}) {
			char *path = obs_module_file(resource);
			if (path) {
				QFontDatabase::addApplicationFont(QString::fromUtf8(path));
				bfree(path);
			}
		}
		return true;
	}();
	Q_UNUSED(registered);
}

QFont display_font(int size, QFont::Weight weight = QFont::Normal)
{
	return {"Science Gothic", size, weight};
}

QFont data_font(int size, QFont::Weight weight = QFont::Normal)
{
	return {"Inter", size, weight};
}
} // namespace

void lol_dashboard_visuals::configure(const lol_dashboard_theme &theme, const lol_dashboard_heatmap &heatmap,
				      const lol_dashboard_regions &regions, int rolling_window_seconds,
				      const QRect &game_frame, const QRect &heatmap_bounds)
{
	theme_ = theme;
	regions_ = regions;
	const bool hex_size_changed = heatmap_.radius != heatmap.radius;
	heatmap_ = heatmap;
	window_ = std::clamp(rolling_window_seconds, 1, 60);
	game_frame_ = game_frame;
	if (heatmap_bounds != heatmap_bounds_ || hex_size_changed)
		resize_heatmap(heatmap_bounds);
}
void lol_dashboard_visuals::resize_heatmap(const QRect &bounds)
{
	heatmap_bounds_ = bounds;
	hex_bins_.clear();
	last_heat_point_.reset();
	const qreal hex_radius = heatmap_.radius;
	const qreal hex_width = std::sqrt(3.0) * hex_radius;
	const int columns = std::max(1, int(std::ceil(bounds.width() / hex_width)) + 1);
	const int rows = std::max(1, int(std::ceil(bounds.height() / (1.5 * hex_radius))) + 1);
	for (int row = 0; row < rows; ++row) {
		const qreal offset = row % 2 ? hex_width / 2.0 : 0.0;
		for (int column = 0; column < columns; ++column)
			hex_bins_.push_back({{bounds.left() + hex_width / 2 + offset + column * hex_width,
					      bounds.top() + hex_radius + row * 1.5 * hex_radius}});
	}
}
void lol_dashboard_visuals::consume(const std::vector<input_data::trace_event> &events,
				    const input_data::button_map<uint16_t> &keyboard,
				    const input_data::button_map<uint16_t> &)
{
	for (const auto &event : events)
		on_event(event);
	const uint64_t now = os_gettime_ns();
	advance(now);
	for (auto it = active_keys_.begin(); it != active_keys_.end();) {
		const auto found = keyboard.find(it->code);
		if (found != keyboard.end() && found->second) {
			held_[it->code] = true;
			++it;
		} else if (it->fade_until && it->fade_until <= now) {
			held_[it->code] = false;
			it = active_keys_.erase(it);
		} else {
			++it;
		}
	}
}
void lol_dashboard_visuals::advance(uint64_t now)
{
	if (!bucket_start_)
		bucket_start_ = now;
	while (now - bucket_start_ >= second_ns) {
		samples_.push_back(current_);
		session_samples_.push_back(current_);
		if (samples_.size() > size_t(window_))
			samples_.pop_front();
		current_.fill(0.0);
		bucket_start_ += second_ns;
	}
}
void lol_dashboard_visuals::on_event(const input_data::trace_event &event)
{
	advance(event.time_ns);
	if (event.type == EVENT_KEY_PRESSED && !held_[event.code]) {
		held_[event.code] = true;
		active_keys_.erase(std::remove_if(active_keys_.begin(), active_keys_.end(),
						  [&](const auto &key) { return key.code == event.code; }),
				   active_keys_.end());
		active_keys_.push_back({event.code, key_label(event.code), 0, ++press_counts_[event.code]});
		++current_[1];
	} else if (event.type == EVENT_KEY_RELEASED) {
		held_[event.code] = false;
		for (auto &key : active_keys_)
			if (key.code == event.code)
				key.fade_until = event.time_ns + 2000000000ULL;
	} else if (event.type == EVENT_MOUSE_PRESSED) {
		++current_[1];
		++total_clicks_;
	}
	if (event.type != EVENT_MOUSE_MOVED && event.type != EVENT_MOUSE_DRAGGED)
		return;
	if (last_motion_)
		current_[0] += std::hypot(event.x - last_motion_->x, event.y - last_motion_->y);
	if (!game_frame_.contains(QPoint(event.x, event.y))) {
		last_motion_ = event;
		last_heat_point_.reset();
		last_distance_.reset();
		return;
	}
	const QPoint relative(event.x - game_frame_.left(), event.y - game_frame_.top());
	if (last_distance_)
		distance_ += std::hypot(relative.x() - last_distance_->x(), relative.y() - last_distance_->y());
	last_distance_ = relative;
	const QPointF point(
		heatmap_bounds_.left() + relative.x() * heatmap_bounds_.width() / std::max(1, game_frame_.width()),
		heatmap_bounds_.top() + relative.y() * heatmap_bounds_.height() / std::max(1, game_frame_.height()));
	if (last_motion_ && last_heat_point_ && event.time_ns > last_motion_->time_ns && !hex_bins_.empty())
		hex_bins_[nearest_hex(*last_heat_point_)].value +=
			std::min(event.time_ns - last_motion_->time_ns, max_heatmap_gap_ns);
	last_motion_ = event;
	last_heat_point_ = point;
}
size_t lol_dashboard_visuals::nearest_hex(const QPointF &point) const
{
	size_t result{};
	qreal best = std::numeric_limits<qreal>::max();
	for (size_t index = 0; index < hex_bins_.size(); ++index) {
		const qreal dx = point.x() - hex_bins_[index].center.x(), dy = point.y() - hex_bins_[index].center.y();
		if (dx * dx + dy * dy < best) {
			best = dx * dx + dy * dy;
			result = index;
		}
	}
	return result;
}
QString lol_dashboard_visuals::key_label(uint16_t code) const
{
	if (code >= VC_A && code <= VC_Z)
		return QString(QChar('A' + code - VC_A));
	if (code >= VC_0 && code <= VC_9)
		return QString(QChar('0' + code - VC_0));
	if (code >= VC_F1 && code <= VC_F12)
		return QString("F%1").arg(code - VC_F1 + 1);
	switch (code) {
	case VC_SPACE:
		return "␣";
	case VC_SHIFT_L:
	case VC_SHIFT_R:
		return "⇧";
	case VC_CONTROL_L:
	case VC_CONTROL_R:
		return "⌃";
	case VC_ALT_L:
	case VC_ALT_R:
		return "⌥";
	case VC_TAB:
		return "⇥";
	case VC_ENTER:
		return "↵";
	case VC_ESCAPE:
		return "⎋";
	case VC_MINUS:
		return "-";
	case VC_EQUALS:
		return "=";
	case VC_OPEN_BRACKET:
		return "[";
	case VC_CLOSE_BRACKET:
		return "]";
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
	default:
		return "?";
	}
}
QString lol_dashboard_visuals::distance_label() const
{
	double value = distance_ / 2800.0 * 2.54;
	QString unit = "cm";
	int decimals = value < 10.0 ? 2 : 1;
	if (value >= 100000.0) {
		value /= 100000.0;
		unit = "km";
		decimals = 3;
	} else if (value >= 1000.0) {
		value /= 100.0;
		unit = "m";
		decimals = 2;
	}
	return QString("%1 %2").arg(value, 0, 'f', decimals).arg(unit);
}
void lol_dashboard_visuals::draw_heatmap(QPainter &painter, const QRect &bounds) const
{
	painter.setClipRect(bounds);
	const qreal hex_radius = heatmap_.radius;
	std::vector<uint64_t> values;
	for (const auto &bin : hex_bins_)
		if (bin.value)
			values.push_back(bin.value);
	std::sort(values.begin(), values.end());
	const uint64_t q1 = values.empty() ? 0 : values[(values.size() - 1) / 4];
	const uint64_t q2 = values.empty() ? 0 : values[(values.size() - 1) / 2];
	const uint64_t q3 = values.empty() ? 0 : values[(values.size() - 1) * 3 / 4];
	for (const auto &bin : hex_bins_) {
		const int band = bin.value <= q1 ? 0 : bin.value <= q2 ? 1 : bin.value <= q3 ? 2 : 3;
		QColor fill = bin.value ? lol_dashboard_heatmap_color(heatmap_, theme_, band) : QColor(Qt::black);
		fill.setAlpha(bin.value ? 150 : 38);
		painter.setBrush(fill);
		painter.setPen(Qt::NoPen);
		if (bin.value)
			painter.setPen(QPen(lol_dashboard_heatmap_color(heatmap_, theme_, band), 0.75));
		QPolygonF hexagon;
		for (int corner = 0; corner < 6; ++corner) {
			const qreal angle = (30.0 + corner * 60.0) * M_PI / 180.0;
			hexagon << QPointF(bin.center.x() + hex_radius * std::cos(angle),
					   bin.center.y() + hex_radius * std::sin(angle));
		}
		painter.drawPolygon(hexagon);
	}
}
void lol_dashboard_visuals::draw_summary(QPainter &painter, const QRect &bounds, bool right_aligned) const
{
	Q_UNUSED(right_aligned);
	painter.setPen(Qt::white);
	painter.setFont(display_font(subtitle_size, QFont::Bold));
	const int label_height = subtitle_size + 8;
	const int value_height = text_size + 12;
	const int group_gap = 10;
	int top = bounds.top();
	const auto alignment = Qt::AlignLeft | Qt::AlignVCenter;
	lol_dashboard_draw_shadowed_text(painter, QRect(bounds.left(), top, bounds.width(), label_height), alignment,
					 obs_module_text("MouseActivity.Distance"));
	top += label_height;
	painter.setFont(data_font(text_size, QFont::Bold));
	painter.setPen(theme_.active);
	lol_dashboard_draw_shadowed_text(painter, QRect(bounds.left(), top, bounds.width(), value_height), alignment,
					 distance_label());
	top += value_height + group_gap;
	painter.setPen(Qt::white);
	painter.setFont(display_font(subtitle_size, QFont::Bold));
	lol_dashboard_draw_shadowed_text(painter, QRect(bounds.left(), top, bounds.width(), label_height), alignment,
					 obs_module_text("MouseActivity.Clicks"));
	top += label_height;
	painter.setFont(data_font(text_size, QFont::Bold));
	painter.setPen(theme_.active);
	lol_dashboard_draw_shadowed_text(painter, QRect(bounds.left(), top, bounds.width(), value_height), alignment,
					 QString::number(total_clicks_));
}
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

void lol_dashboard_visuals::draw_intensity(QPainter &painter, const QRect &bounds) const
{
	constexpr int intensity_spacing = 60;
	const int card_width = std::max(1, (bounds.width() - intensity_spacing) / 2);
	for (int metric = 0; metric < 2; ++metric) {
		const QRect card(bounds.left() + metric * (card_width + intensity_spacing), bounds.top(), card_width,
				 bounds.height());
		std::vector<double> values;
		for (const auto &sample : session_samples_)
			values.push_back(metric == 0 ? sample[0] / 2800.0 * 2.54 : sample[1] * 60.0);
		std::array<double, 2> total = current_;
		for (const auto &sample : samples_) {
			total[metric] += sample[metric];
		}
		const double current = metric == 0 ? total[0] / window_ / 2800.0 * 2.54 : total[1] * 60.0 / window_;
		values.push_back(current);
		std::sort(values.begin(), values.end());
		const double min = values.front(), max = values.back(), range = std::max(0.001, max - min);
		const auto x = [&](double value) {
			return card.left() + 8 + int((value - min) / range * std::max(1, card.width() - 16));
		};
		const double q1 = values[(values.size() - 1) / 4], median = values[(values.size() - 1) / 2],
			     q3 = values[(values.size() - 1) * 3 / 4];
		const int y = card.top() + std::max(14, card.height() / 4);
		painter.setPen(Qt::white);
		painter.drawLine(x(min), y, x(max), y);
		painter.setBrush(theme_.inactive);
		painter.drawRect(QRect(std::min(x(q1), x(q3)), y - 7, std::max(1, std::abs(x(q3) - x(q1))), 14));
		painter.drawLine(x(median), y - 8, x(median), y + 8);
		painter.setPen(QPen(theme_.active, 3));
		painter.drawLine(x(current), y - 13, x(current), y + 13);
		painter.setPen(Qt::white);
		painter.setFont(data_font(subtitle_size));
		lol_dashboard_draw_shadowed_text(painter,
						 QRect(card.left() + 6, y + 14, card.width() - 12, subtitle_size),
						 Qt::AlignLeft, QString::number(min, 'f', min < 10 ? 1 : 0));
		lol_dashboard_draw_shadowed_text(painter,
						 QRect(card.left() + 6, y + 14, card.width() - 12, subtitle_size),
						 Qt::AlignRight, QString::number(max, 'f', max < 10 ? 1 : 0));
		painter.setFont(display_font(title_size, QFont::Bold));
		lol_dashboard_draw_shadowed_text(painter,
						 QRect(card.left(), y + 14 + subtitle_size, card.width(), title_size),
						 Qt::AlignHCenter,
						 obs_module_text(metric ? "LoLPerformanceDashboard.APM"
									: "LoLPerformanceDashboard.MouseVelocity"));
		painter.setFont(data_font(text_size, QFont::Bold));
		painter.setPen(theme_.active);
		lol_dashboard_draw_shadowed_text(
			painter, QRect(card.left(), y + 14 + subtitle_size + title_size, card.width(), text_size),
			Qt::AlignHCenter, QString::number(current, 'f', current < 10 ? 1 : 0));
	}
}

void lol_dashboard_visuals::draw(QPainter &painter, const QRect &header, const QRect &heatmap, const QRect &summary,
				 const QRect &keys, bool right_aligned) const
{
	ensure_dashboard_fonts_registered();
	painter.fillRect(QRect(0, 0, std::max({header.right(), heatmap.right(), summary.right(), keys.right()}) + 1,
			       std::max({header.bottom(), heatmap.bottom(), summary.bottom(), keys.bottom()}) + 1),
			 theme_.background);
	if (regions_.intensity)
		draw_intensity(painter, header);
	if (regions_.mouse_activity)
		draw_heatmap(painter, heatmap);
	painter.setClipping(false);
	if (regions_.mouse_activity)
		draw_summary(painter, summary, right_aligned);
	if (regions_.keys)
		draw_keys(painter, keys, right_aligned);
}

} // namespace sources
